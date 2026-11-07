"""The vision policy: a small residual CNN over a stack of recent frames,
conditioned on who is playing where and on its own recent inputs, with one
head per action component.

Inputs
  frames   (B, K*3, H, W) uint8 -- the last K presented frames, oldest first
  own      (B,) own CharacterKind         opp    (B,) opponent's
  stage    (B,) dense stage index (ST.INDEX)   slot   (B,) 0 or 1
  h_stick  (B, HIST) stick classes of the last HIST frames, oldest first
  h_cstick (B, HIST)    h_buttons (B, HIST, 5)

The action history matters more than it looks: the CPU's timing runs on
cooldowns, and "how long since I last attacked" is the only view a policy
gets of them (measured: next-frame accuracy on input-change frames goes
from 0.16 to 0.27 with it).

Outputs: logits for the next frame's stick (17), cstick (5) and buttons (5,
independent sigmoids); the same for a window of the next frames (auxiliary,
training only: "will the CPU press A within 10 frames" is far more
predictable than "will it press A next frame", and learning it shapes the
features the next-frame heads use); and a value estimate for later RL.

One network for every character: the shared trunk learns the game, the
embeddings learn what each character's inputs do.
"""
import torch
import torch.nn as nn
import torch.nn.functional as F

import actions
from melee_env import CK, ST

HIST = 16     # frames of own action history fed back in
WINDOW = 10   # frames the auxiliary targets look ahead


class ResBlock(nn.Module):
    def __init__(self, c):
        super().__init__()
        self.c1 = nn.Conv2d(c, c, 3, padding=1)
        self.c2 = nn.Conv2d(c, c, 3, padding=1)

    def forward(self, x):
        y = self.c1(F.relu(x))
        y = self.c2(F.relu(y))
        return x + y


class ImpalaStage(nn.Module):
    def __init__(self, cin, cout):
        super().__init__()
        self.conv = nn.Conv2d(cin, cout, 3, padding=1)
        self.r1 = ResBlock(cout)
        self.r2 = ResBlock(cout)

    def forward(self, x):
        x = self.conv(x)
        x = F.max_pool2d(x, 3, stride=2, padding=1)
        return self.r2(self.r1(x))


class Policy(nn.Module):
    def __init__(self, frames=3, hist=HIST, width=(32, 64, 64, 128), emb=16, hidden=512):
        super().__init__()
        self.frames, self.hist = frames, hist
        stages, cin = [], frames * 3
        for c in width:
            stages.append(ImpalaStage(cin, c))
            cin = c
        self.stages = nn.Sequential(*stages)
        self.pool = nn.AdaptiveAvgPool2d((3, 4))
        self.own = nn.Embedding(CK.COUNT, emb)
        self.opp = nn.Embedding(CK.COUNT, emb)
        self.stage = nn.Embedding(len(ST.ALL), emb)
        self.slot = nn.Embedding(2, emb)
        self.h_stick = nn.Embedding(actions.N_STICK, 8)
        self.h_cstick = nn.Embedding(actions.N_CSTICK, 4)
        self.h_buttons = nn.Linear(actions.N_BUTTONS, 8)
        self.h_fc = nn.Sequential(nn.Linear(hist * 20, 128), nn.ReLU())
        self.fc = nn.Sequential(
            nn.Linear(cin * 12 + emb * 4 + 128, hidden), nn.ReLU(),
            nn.Linear(hidden, hidden), nn.ReLU())
        self.head_stick = nn.Linear(hidden, actions.N_STICK)
        self.head_cstick = nn.Linear(hidden, actions.N_CSTICK)
        self.head_buttons = nn.Linear(hidden, actions.N_BUTTONS)
        self.head_w_stick = nn.Linear(hidden, actions.N_STICK)
        self.head_w_buttons = nn.Linear(hidden, actions.N_BUTTONS)
        self.head_value = nn.Linear(hidden, 1)

    def features(self, frames, own, opp, stage, slot, h_stick, h_cstick, h_buttons):
        x = frames.float() / 255.0
        x = self.stages(x)
        x = self.pool(F.relu(x)).flatten(1)
        h = torch.cat([self.h_stick(h_stick), self.h_cstick(h_cstick),
                       self.h_buttons(h_buttons.float())], dim=2).flatten(1)
        cond = torch.cat([self.own(own), self.opp(opp), self.stage(stage), self.slot(slot),
                          self.h_fc(h)], dim=1)
        return self.fc(torch.cat([x, cond], dim=1))

    def forward(self, *inputs):
        h = self.features(*inputs)
        return {"stick": self.head_stick(h), "cstick": self.head_cstick(h),
                "buttons": self.head_buttons(h), "w_stick": self.head_w_stick(h),
                "w_buttons": self.head_w_buttons(h), "value": self.head_value(h).squeeze(1)}

    @torch.no_grad()
    def act(self, *inputs, sample=True, temperature=1.0):
        o = self.forward(*inputs)
        ls, lc, lb = o["stick"], o["cstick"], o["buttons"]
        if sample:
            stick = torch.distributions.Categorical(logits=ls / temperature).sample()
            cstick = torch.distributions.Categorical(logits=lc / temperature).sample()
            buttons = (torch.rand_like(lb) < torch.sigmoid(lb / temperature)).to(torch.uint8)
        else:
            stick, cstick = ls.argmax(1), lc.argmax(1)
            buttons = (lb > 0).to(torch.uint8)
        return stick, cstick, buttons, o["value"]


def bc_loss(out, y, change_weight=4.0, aux_weight=0.5, label_smoothing=0.0):
    """y: dict with stick, cstick, buttons (next frame), w_stick, w_buttons
    (window), and prev_stick, prev_buttons (to weight the frames where the
    input changes, which are the ones that carry any information)."""
    w_s = 1.0 + change_weight * (y["stick"] != y["prev_stick"]).float()
    w_b = 1.0 + change_weight * (y["buttons"] != y["prev_buttons"]).any(1).float()
    l_s = (F.cross_entropy(out["stick"], y["stick"], reduction="none",
                           label_smoothing=label_smoothing) * w_s).sum() / w_s.sum()
    l_c = F.cross_entropy(out["cstick"], y["cstick"], label_smoothing=label_smoothing)
    l_b = (F.binary_cross_entropy_with_logits(out["buttons"], y["buttons"].float(),
                                              reduction="none").mean(1) * w_b).sum() / w_b.sum()
    l_ws = F.cross_entropy(out["w_stick"], y["w_stick"])
    l_wb = F.binary_cross_entropy_with_logits(out["w_buttons"], y["w_buttons"].float())
    total = l_s + l_c + l_b + aux_weight * (l_ws + l_wb)
    return total, {"stick": l_s.item(), "cstick": l_c.item(), "buttons": l_b.item(),
                   "w_stick": l_ws.item(), "w_buttons": l_wb.item()}

"""Sequence policies: every frame becomes a token (the CNN's view of the
picture plus the action taken on it), and a sequence mixer over the tokens
decides the next input. Three mixers, same tokens, same heads, same loss:

  transformer  causal attention over the last SEQ tokens. Memory of record:
               a verbatim log of the last ~2 s, re-read every frame; nothing
               older survives.
  ssm          Mamba-style selective state-space blocks. No log: one running
               state per layer, rewritten every frame by input-dependent
               gates, so it can hold something for a whole match or
               overwrite it. Trained in parallel over windows by scanning;
               played one frame at a time at constant cost.
  rmt          the transformer plus MEM persistent memory tokens (recurrent
               memory transformer). Attention sees the current segment and
               the memory; at the end of each SEG-frame segment the model
               writes new memory tokens that carry into the next segment, so
               it keeps a log of the recent past and learns to write down
               what to keep from the older past.

Tokens
  frame feature   CNN trunk from model.py, one frame per token, projected to D
  action          what was pressed on that frame (stick, cstick, buttons)
  position        learned (transformer, rmt): the current frame last
  context         own/opp character, stage, slot: added to every token

Heads (per position): next-frame stick / cstick / buttons, the WINDOW
auxiliaries, and a value for later RL.
"""
import math

import torch
import torch.nn as nn
import torch.nn.functional as F

import actions
from melee_env import CK, ST
from model import ImpalaStage, WINDOW

SEQ = 128
MEM = 8      # rmt: persistent memory tokens
SEG = 64     # rmt: frames per segment


class Tokens(nn.Module):
    """The shared front end: pictures and actions to D-dim tokens."""

    def __init__(self, width=(32, 64, 64, 128), d=256, emb=16):
        super().__init__()
        self.d = d
        stages, cin = [], 3
        for c in width:
            stages.append(ImpalaStage(cin, c))
            cin = c
        self.stages = nn.Sequential(*stages)
        self.pool = nn.AdaptiveAvgPool2d((3, 4))
        self.proj = nn.Linear(cin * 12, d)
        self.a_stick = nn.Embedding(actions.N_STICK, d)
        self.a_cstick = nn.Embedding(actions.N_CSTICK, d)
        self.a_buttons = nn.Linear(actions.N_BUTTONS, d)
        self.own = nn.Embedding(CK.COUNT, emb)
        self.opp = nn.Embedding(CK.COUNT, emb)
        self.stage = nn.Embedding(len(ST.ALL), emb)
        self.slot = nn.Embedding(2, emb)
        self.ctx = nn.Linear(emb * 4, d)

    def encode(self, frames):
        """(N, 3, H, W) uint8 -> (N, D). Cached per frame at play time."""
        x = self.stages(frames.float() / 255.0)
        return self.proj(self.pool(F.relu(x)).flatten(1))

    def build(self, feats, a_stick, a_cstick, a_buttons, own, opp, stage, slot):
        tok = feats + self.a_stick(a_stick) + self.a_cstick(a_cstick) + self.a_buttons(a_buttons.float())
        ctx = self.ctx(torch.cat([self.own(own), self.opp(opp), self.stage(stage), self.slot(slot)], 1))
        return tok + ctx.unsqueeze(1)


class Heads(nn.Module):
    def __init__(self, d):
        super().__init__()
        self.norm = nn.LayerNorm(d)
        self.stick = nn.Linear(d, actions.N_STICK)
        self.cstick = nn.Linear(d, actions.N_CSTICK)
        self.buttons = nn.Linear(d, actions.N_BUTTONS)
        self.w_stick = nn.Linear(d, actions.N_STICK)
        self.w_buttons = nn.Linear(d, actions.N_BUTTONS)
        self.value = nn.Linear(d, 1)

    def forward(self, h):
        h = self.norm(h)
        return {"stick": self.stick(h), "cstick": self.cstick(h), "buttons": self.buttons(h),
                "w_stick": self.w_stick(h), "w_buttons": self.w_buttons(h),
                "value": self.value(h).squeeze(-1)}


def _sample(o, sample, temperature):
    ls, lc, lb = o["stick"], o["cstick"], o["buttons"]
    if sample:
        stick = torch.distributions.Categorical(logits=ls / temperature).sample()
        cstick = torch.distributions.Categorical(logits=lc / temperature).sample()
        buttons = (torch.rand_like(lb) < torch.sigmoid(lb / temperature)).to(torch.uint8)
    else:
        stick, cstick = ls.argmax(-1), lc.argmax(-1)
        buttons = (lb > 0).to(torch.uint8)
    return stick, cstick, buttons, o["value"]


# ---------------------------------------------------------------- transformer
class SeqPolicy(nn.Module):
    arch = "transformer"

    def __init__(self, seq=SEQ, d=256, layers=4, heads=4, **kw):
        super().__init__()
        self.seq, self.d = seq, d
        self.tokens = Tokens(d=d)
        self.pos = nn.Embedding(seq, d)
        layer = nn.TransformerEncoderLayer(d, heads, dim_feedforward=4 * d, dropout=0.1,
                                           batch_first=True, norm_first=True, activation="gelu")
        self.tf = nn.TransformerEncoder(layer, layers, enable_nested_tensor=False)
        self.heads = Heads(d)
        self.register_buffer("mask", torch.triu(torch.full((seq, seq), float("-inf")), 1), persistent=False)

    def encode(self, frames):
        return self.tokens.encode(frames)

    def decode(self, feats, *rest):
        """feats (B, L, D) with L <= seq -> head dict of (B, L, *)."""
        L = feats.shape[1]
        tok = self.tokens.build(feats, *rest)
        # The current frame sits at the last position so that a short history
        # at match start and a full one later both end where the heads read.
        tok = tok + self.pos.weight[self.seq - L:].unsqueeze(0)
        return self.heads(self.tf(tok, mask=self.mask[:L, :L], is_causal=True))

    def forward(self, frames, *rest):
        """frames (B, L, 3, H, W) uint8."""
        B, L = frames.shape[:2]
        feats = self.encode(frames.flatten(0, 1)).view(B, L, self.d)
        return self.decode(feats, *rest)

    @torch.no_grad()
    def act(self, feats, *rest, sample=True, temperature=1.0):
        o = {k: v[:, -1] for k, v in self.decode(feats, *rest).items()}
        return _sample(o, sample, temperature)


# ------------------------------------------------------------------------ ssm
class SSMBlock(nn.Module):
    """A Mamba-style selective SSM block in plain PyTorch.

    x -> in_proj -> (u, z); u -> causal conv (k=4) -> SiLU -> selective scan
    with input-dependent step (delta), B and C -> gated by SiLU(z) -> out_proj.
    The scan is a loop over time: one state update per frame, which is also
    exactly what `step` does at play time. Slow next to the CUDA kernels in
    mamba-ssm, but those need a toolchain this venv does not have, and the
    CNN over the frames costs more than this loop does.
    """

    def __init__(self, d, expand=2, n_state=16, k=4):
        super().__init__()
        self.d, self.di, self.n, self.k = d, d * expand, n_state, k
        self.norm = nn.LayerNorm(d)
        self.in_proj = nn.Linear(d, 2 * self.di)
        self.conv = nn.Conv1d(self.di, self.di, k, groups=self.di, padding=k - 1)
        self.x_proj = nn.Linear(self.di, 2 * n_state + 1)       # B, C, delta (rank-1)
        self.dt_proj = nn.Linear(1, self.di)
        # A < 0 on a log scale so the state decays; initialised 1..N as Mamba does.
        self.A_log = nn.Parameter(torch.log(torch.arange(1, n_state + 1, dtype=torch.float32)).repeat(self.di, 1))
        self.D = nn.Parameter(torch.ones(self.di))
        self.out_proj = nn.Linear(self.di, d)
        with torch.no_grad():
            self.dt_proj.bias.fill_(math.log(math.e ** 0.01 - 1))   # softplus^-1(0.01)

    def _gates(self, u):
        """u (B, L, Di) -> dA (B, L, Di, N), dBu (B, L, Di, N), C (B, L, N)."""
        bcd = self.x_proj(u)
        Bm, Cm, dt = bcd[..., :self.n], bcd[..., self.n:2 * self.n], bcd[..., 2 * self.n:]
        delta = F.softplus(self.dt_proj(dt))                         # (B, L, Di)
        A = -torch.exp(self.A_log.float())                           # (Di, N)
        dA = torch.exp(delta.unsqueeze(-1) * A)                      # (B, L, Di, N)
        dBu = delta.unsqueeze(-1) * Bm.unsqueeze(2) * u.unsqueeze(-1)
        return dA, dBu, Cm

    def forward(self, x):
        """x (B, L, D) -> (B, L, D), causal."""
        B, L, _ = x.shape
        u, z = self.in_proj(self.norm(x)).chunk(2, dim=-1)
        u = F.silu(self.conv(u.transpose(1, 2))[:, :, :L].transpose(1, 2))
        dA, dBu, Cm = self._gates(u)
        h = u.new_zeros(B, self.di, self.n)
        ys = []
        for t in range(L):
            h = dA[:, t] * h + dBu[:, t]
            ys.append((h * Cm[:, t].unsqueeze(1)).sum(-1))
        y = torch.stack(ys, 1) + u * self.D
        return x + self.out_proj(y * F.silu(z))

    def init_state(self, B, device):
        return {"h": torch.zeros(B, self.di, self.n, device=device),
                "conv": torch.zeros(B, self.di, self.k - 1, device=device)}

    def step(self, x, st):
        """x (B, D) one frame -> (B, D), state updated in place."""
        u, z = self.in_proj(self.norm(x)).chunk(2, dim=-1)
        win = torch.cat([st["conv"], u.unsqueeze(-1)], -1)          # (B, Di, k)
        st["conv"] = win[:, :, 1:]
        u = F.silu((win * self.conv.weight.squeeze(1)).sum(-1) + self.conv.bias)
        dA, dBu, Cm = self._gates(u.unsqueeze(1))
        st["h"] = dA[:, 0] * st["h"] + dBu[:, 0]
        y = (st["h"] * Cm[:, 0].unsqueeze(1)).sum(-1) + u * self.D
        return x + self.out_proj(y * F.silu(z))


class SSMPolicy(nn.Module):
    arch = "ssm"

    def __init__(self, d=256, layers=4, **kw):
        super().__init__()
        self.d = d
        self.tokens = Tokens(d=d)
        self.blocks = nn.ModuleList([SSMBlock(d) for _ in range(layers)])
        self.heads = Heads(d)

    def encode(self, frames):
        return self.tokens.encode(frames)

    def decode(self, feats, *rest):
        h = self.tokens.build(feats, *rest)
        for b in self.blocks:
            h = b(h)
        return self.heads(h)

    def forward(self, frames, *rest):
        B, L = frames.shape[:2]
        feats = self.encode(frames.flatten(0, 1)).view(B, L, self.d)
        return self.decode(feats, *rest)

    def init_state(self, B, device):
        return [b.init_state(B, device) for b in self.blocks]

    @torch.no_grad()
    def act(self, feat, a_stick, a_cstick, a_buttons, own, opp, stage, slot, state,
            sample=True, temperature=1.0):
        """One frame: feat (B, D), actions (B[, 5]); state from init_state."""
        h = self.tokens.build(feat.unsqueeze(1), a_stick.unsqueeze(1), a_cstick.unsqueeze(1),
                              a_buttons.unsqueeze(1), own, opp, stage, slot)[:, 0]
        for b, st in zip(self.blocks, state):
            h = b.step(h, st)
        return _sample(self.heads(h), sample, temperature)


# ------------------------------------------------------------------------ rmt
class RMTPolicy(nn.Module):
    """Transformer over segments with MEM memory tokens carried between them.

    Layout of one segment's attention input: [read memory | frames | write
    memory]. Frames attend causally to earlier frames and to the read memory;
    the write tokens attend to everything, and their outputs become the next
    segment's read memory (gradient flows across segments in training).
    """
    arch = "rmt"

    def __init__(self, seg=SEG, mem=MEM, d=256, layers=4, heads=4, **kw):
        super().__init__()
        self.seg, self.mem, self.d = seg, mem, d
        self.tokens = Tokens(d=d)
        self.pos = nn.Embedding(seg, d)
        self.mem0 = nn.Parameter(torch.randn(mem, d) * 0.02)
        self.mem_pos = nn.Parameter(torch.randn(2, mem, d) * 0.02)    # read / write tags
        layer = nn.TransformerEncoderLayer(d, heads, dim_feedforward=4 * d, dropout=0.1,
                                           batch_first=True, norm_first=True, activation="gelu")
        self.tf = nn.TransformerEncoder(layer, layers, enable_nested_tensor=False)
        self.heads = Heads(d)

    def _mask(self, L, device):
        M, n = self.mem, self.mem * 2 + L
        m = torch.zeros(n, n, device=device)
        # frames: causal among themselves, see read memory, never write tokens
        fr = torch.triu(torch.full((L, L), float("-inf"), device=device), 1)
        m[M:M + L, M:M + L] = fr
        m[M:M + L, M + L:] = float("-inf")
        # read memory: sees only itself
        m[:M, M:] = float("-inf")
        return m

    def encode(self, frames):
        return self.tokens.encode(frames)

    def init_memory(self, B):
        return self.mem0.unsqueeze(0).expand(B, -1, -1)

    def segment(self, memory, feats, *rest):
        """memory (B, MEM, D), feats (B, L<=seg, D) -> (heads over frames, new memory)."""
        B, L, _ = feats.shape
        tok = self.tokens.build(feats, *rest) + self.pos.weight[:L].unsqueeze(0)
        read = memory + self.mem_pos[0]
        write = memory + self.mem_pos[1]
        h = self.tf(torch.cat([read, tok, write], 1), mask=self._mask(L, feats.device))
        M = self.mem
        return self.heads(h[:, M:M + L]), h[:, M + L:]

    def decode(self, feats, *rest):
        """Whole window: consecutive segments with memory carried across."""
        B, L, _ = feats.shape
        memory = self.init_memory(B)
        outs = []
        for s in range(0, L, self.seg):
            sl = slice(s, s + self.seg)
            o, memory = self.segment(memory, feats[:, sl], rest[0][:, sl], rest[1][:, sl], rest[2][:, sl], *rest[3:])
            outs.append(o)
        return {k: torch.cat([o[k] for o in outs], 1) for k in outs[0]}

    def forward(self, frames, *rest):
        B, L = frames.shape[:2]
        feats = self.encode(frames.flatten(0, 1)).view(B, L, self.d)
        return self.decode(feats, *rest)

    @torch.no_grad()
    def act(self, memory, feats, *rest, sample=True, temperature=1.0):
        """feats: the current (partial) segment. Returns the action and, when
        the segment is full, the memory for the next one (else None)."""
        o, new_mem = self.segment(memory, feats, *rest)
        o = {k: v[:, -1] for k, v in o.items()}
        return _sample(o, sample, temperature), (new_mem if feats.shape[1] >= self.seg else None)


ARCHS = {"transformer": SeqPolicy, "ssm": SSMPolicy, "rmt": RMTPolicy}


def build(arch, **kw):
    return ARCHS[arch](**kw)


def seq_loss(out, y, change_weight=4.0, aux_weight=0.5):
    """Same terms as model.bc_loss, over every position of every sequence."""
    flat = lambda t: t.flatten(0, 1)
    stick, prev_stick = flat(y["stick"]), flat(y["prev_stick"])
    buttons, prev_buttons = flat(y["buttons"]), flat(y["prev_buttons"])
    w_s = 1.0 + change_weight * (stick != prev_stick).float()
    w_b = 1.0 + change_weight * (buttons != prev_buttons).any(1).float()
    l_s = (F.cross_entropy(flat(out["stick"]), stick, reduction="none") * w_s).sum() / w_s.sum()
    l_c = F.cross_entropy(flat(out["cstick"]), flat(y["cstick"]))
    l_b = (F.binary_cross_entropy_with_logits(flat(out["buttons"]), buttons.float(),
                                              reduction="none").mean(1) * w_b).sum() / w_b.sum()
    l_ws = F.cross_entropy(flat(out["w_stick"]), flat(y["w_stick"]))
    l_wb = F.binary_cross_entropy_with_logits(flat(out["w_buttons"]), flat(y["w_buttons"]).float())
    total = l_s + l_c + l_b + aux_weight * (l_ws + l_wb)
    return total, {"stick": l_s.item(), "cstick": l_c.item(), "buttons": l_b.item(),
                   "w_stick": l_ws.item(), "w_buttons": l_wb.item()}

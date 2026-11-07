"""Game-state observations: what a state policy sees instead of the picture.

One row of K numbers per fighter, the same whether it comes from the live
port (melee_env.Obs) or from a Slippi replay (slp_convert.py):

  0 char      external CKind (Fox 2, Falco 20, ...)
  1 motion    action state id (fp->motion_id; Slippi's post-frame state)
  2 animf     frame within the action state (cur_anim_frame; Slippi's state age)
  3 x         position, game units
  4 y
  5 face      -1 left, +1 right
  6 vx        self-induced velocity (fp->self_vel; Slippi's self air x)
  7 vy        (Slippi's self y)
  8 airborne  1 in the air
  9 pct       damage percent
 10 stocks

A model's input for one frame is [own row, opponent row] flattened to 2K.
Collected chunks store the rows as `pstate` (T, 2, K) float32 by slot.
"""
import numpy as np

K = 11
N_MOTION = 512     # action state ids run to ~400 including character specials


def player_row(p):
    return [p.char, p.motion, p.animf, p.x, p.y, p.face, p.vx, p.vy,
            1.0 if p.airborne else 0.0, p.pct, p.stocks]


def obs_rows(obs):
    """(2, K) for slots 0 and 1 of a live Obs."""
    return np.array([player_row(obs.players[0]), player_row(obs.players[1])], np.float32)


def model_input(rows, own):
    """(..., 2, K) by slot -> (..., 2K) with the acting fighter first."""
    return np.concatenate([rows[..., own, :], rows[..., 1 - own, :]], -1)

# Multiplayer Networking Guide

This guide covers OpenQ4 multiplayer networking behavior and the cvars used to tune or revert prediction/lag-comp behavior.

## Quick Summary

- Server-side hitscan lag compensation is enabled by default.
- Remote-client prediction runs in enhanced mode by default.
- Both systems can be switched back to legacy behavior with cvars.

## CVar Reference

| Setting | Default | Range | Scope | What it does |
|---|---:|---:|---|---|
| `net_mpLagCompensation` | `1` | `0..1` | Server gameplay | Enables server-side lag compensation for multiplayer hitscan traces. |
| `net_mpLagCompMaxMS` | `200` | `0..1000` | Server gameplay | Caps rewind window in milliseconds used by lag compensation. |
| `net_mpLagCompBiasMS` | `0` | `-200..200` | Server gameplay | Adds/subtracts additional rewind bias in milliseconds. |
| `net_mpLagCompDebug` | `0` | `0..2` | Server gameplay | Debug logging for lag compensation (`0` off, `1` summary, `2` verbose). |
| `net_mpPredictMode` | `1` | `0..1` | MP client prediction | Selects remote-player prediction mode (`0` legacy limited, `1` enhanced per-frame). |

## Legacy Compatibility Switch

Use this to restore legacy multiplayer behavior:

```cfg
seta net_mpLagCompensation 0
seta net_mpPredictMode 0
```

## Recommended Starting Presets

### Default Internet Play

```cfg
seta net_mpLagCompensation 1
seta net_mpLagCompMaxMS 200
seta net_mpLagCompBiasMS 0
seta net_mpPredictMode 1
```

### Low-Latency/LAN

```cfg
seta net_mpLagCompensation 1
seta net_mpLagCompMaxMS 80
seta net_mpLagCompBiasMS 0
seta net_mpPredictMode 1
```

## Notes

- Lag compensation applies to authoritative multiplayer hitscan traces on the server.
- `net_mpLagCompDebug` output is intended for server diagnostics and tuning.
- Tune `net_mpLagCompMaxMS` before using large `net_mpLagCompBiasMS` offsets.

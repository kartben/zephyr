These SVG assets are sourced from [Phosphor Icons](https://phosphoricons.com/), using the official `regular` set from the upstream repository:

- Repository: <https://github.com/phosphor-icons/core>
- Asset path: `assets/regular/*.svg`
- License: MIT

Local card files keep Zephyr-specific names, but map to these upstream icons:

- `openrisc-support.svg`: `cpu.svg`
- `toolchain-sdk-c17.svg`: `terminal-window.svg`
- `wifi-p2p.svg`: `wifi-high.svg`
- `wireguard-vpn.svg`: kept for fallback, but the card now uses the dragon emblem cropped from the official `wireguard.com/img/wireguard.svg` asset
- `wireguard-official.svg`: vendored official WireGuard logo asset from `https://www.wireguard.com/img/wireguard.svg`
- `usb-host.svg`: `usb.svg`
- `otp-memory-api.svg`: `memory.svg`
- `biometrics-api.svg`: `fingerprint.svg`
- `zbus-proxy-agents.svg`: `share-network.svg`
- `pressure-based-cpu-frequency-scaling.svg`: `gauge.svg`
- `cortex-m-context-switching.svg`: `arrows-clockwise.svg`
- `html-dashboard.svg`: kept for fallback, but the card now uses a custom vector dashboard mock based on the HTML report UI
- `scope-based-cleanup-helpers.svg`: kept for fallback, but the card now uses a custom code-snippet illustration based on `doc/kernel/cleanup.rst`
- `ztest-benchmarking-framework.svg`: `timer.svg`
- `expanded-board-support.svg`: `stack.svg`

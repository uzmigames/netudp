# 09. Sources

All external references used in this analysis.

## Game Networking Benchmarks

| Source | URL | Used For |
|--------|-----|---------|
| NetworkBenchmarkDotNet v0.8.2 | https://github.com/JohannesDeml/NetworkBenchmarkDotNet | ENet, LiteNetLib, Kcp2k PPS numbers |
| BenchmarkNet (Unity forum) | https://discussions.unity.com/t/benchmarknet-stress-test-for-enet-unet-litenetlib-lidgren-miniudp-hazel-photon-and-others/688315 | Additional benchmark data |
| ENet-CSharp | https://github.com/nxrighthere/ENet-CSharp | ENet protocol overhead |
| reliable_udp_bench_mark | https://github.com/libinzhangyuan/reliable_udp_bench_mark/blob/master/bench_mark.md | KCP vs ENet latency comparison |

## Game Networking Libraries

| Source | URL | Used For |
|--------|-----|---------|
| GameNetworkingSockets Issue #198 | https://github.com/ValveSoftware/GameNetworkingSockets/issues/198 | GNS throughput measurement |
| GNS v1.3.0 release notes | https://github.com/ValveSoftware/GameNetworkingSockets/releases/tag/v1.3.0 | Multi-thread improvements |
| yojimbo | https://github.com/mas-bandwidth/yojimbo | Architecture reference |
| netcode.io STANDARD.md | https://github.com/mas-bandwidth/netcode | Protocol spec, client count targets |
| Gaffer On Games | https://gafferongames.com/post/reliable_ordered_messages/ | Design philosophy, send rates |
| kcp-go benchmarks | https://github.com/xtaci/kcp-go | KCP throughput + encryption benchmarks |
| skywind3000/kcp wiki | https://github.com/skywind3000/kcp/wiki/EN_KCP-Benchmark | KCP benchmark data |
| LiteNetLib | https://revenantx.github.io/LiteNetLib/index.html | Protocol overhead |
| Mirror Networking (KCP) | https://mirror-networking.gitbook.io/docs/manual/transports/kcp-transport | KCP usage in Unity |

## .NET Socket Benchmarks

| Source | URL | Used For |
|--------|-----|---------|
| Enclave: UDP in .NET 8 | https://enclave.io/high-performance-udp-sockets-net8/ | .NET 8 socket PPS |
| Enclave: UDP in .NET 6 | https://enclave.io/high-performance-udp-sockets-net6/ | .NET 6 baseline |
| enclave-networks/research.udp-perf | https://github.com/enclave-networks/research.udp-perf | Benchmark source code |

## Linux Kernel Networking

| Source | URL | Used For |
|--------|-----|---------|
| Cloudflare: Million Packets | https://blog.cloudflare.com/how-to-receive-a-million-packets/ | Linux socket ceiling, SO_REUSEPORT |
| APNIC: BPF UDP Server | https://blog.apnic.net/2023/10/19/rocky-road-towards-ultimate-udp-server-with-bpf-based-load-balancing-on-linux-part-2/ | BPF load balancing numbers |
| NordVarg: Kernel Bypass | https://nordvarg.com/blog/kernel-bypass-networking | DPDK vs io_uring vs sockets comparison |
| MoonGen 10M PPS | https://serverascode.com/2018/12/31/ten-million-packets-per-second.html | DPDK ceiling reference |
| toonk.io: DPDK traffic gen | https://toonk.io/building-a-high-performance-linux-based-traffic-generator-with-dpdk/index.html | DPDK implementation reference |

## Encryption Performance

| Source | URL | Used For |
|--------|-----|---------|
| Ash's Blog: AES vs ChaCha20 (2025) | https://ashvardanian.com/posts/chacha-vs-aes-2025/ | Per-CPU crypto comparison |
| BearSSL speed benchmarks | https://www.bearssl.org/speed.html | Crypto cycles/byte reference |
| Red Hat: IPSec AES-GCM on RHEL 9 | https://www.redhat.com/en/blog/ipsec-performance-red-hat-enterprise-linux-9-performance-analysis-aes-gcm | AES-NI throughput |

## Game Engine Networking

| Source | URL | Used For |
|--------|-----|---------|
| UT2004 Traffic Analysis | https://telematics.tm.kit.edu/publications/Files/295/ut2004traffic.pdf | Unreal Engine network traffic measurement |
| UE4 Bandwidth/MTU guide | https://ikrima.dev/ue4guide/wip/unfinished/networking-bandwidth-mtu/ | Unreal MTU and bandwidth config |
| UTP 2.0 FAQ | https://docs-multiplayer.unity3d.com/transport/current/utp-2-faq/index.html | Unity Transport Protocol details |

## Internal Analysis (this project)

| Source | Path | Used For |
|--------|------|---------|
| QueueBuffer analysis | `docs/analysis/mmorpg-server/03-queuebuffer.md` | Legacy TS server batching pattern |
| Position quantization | `docs/analysis/server/20-position-quantization-and-delta-encoding.md` | Delta encoding patterns from Server1 |
| Entity system (Server5) | `docs/analysis/server5/17-entity-system.md` | DeltaSync packet type reference |
| GNS analysis | `docs/analysis/gns/` | Valve networking architecture |
| netcode.io analysis | `docs/analysis/netcode-io/` | Auth protocol patterns |
| Unreal Engine analysis | `docs/analysis/unreal-engine/` | Replication system architecture |

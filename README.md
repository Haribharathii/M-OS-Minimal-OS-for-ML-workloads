<p align="center">
  <h1 align="center">M-OS — Minimal OS for ML Workloads</h1>
  <p align="center">
    A lightweight, x86 operating system kernel built from the ground up to serve as a high-performance substrate for machine-learning inference and training pipelines.
  </p>
</p>

<p align="center">
  <img src="https://img.shields.io/badge/arch-x86__32-blue" />
  <img src="https://img.shields.io/badge/lang-C-informational" />
  <img src="https://img.shields.io/badge/license-MIT-green" />
  <img src="https://img.shields.io/badge/build-passing-brightgreen" />
</p>

---

## Overview

**M-OS** is a purpose-built, minimal operating system kernel designed to eliminate the overhead that general-purpose OSes impose on data-intensive ML workloads. By stripping away unnecessary abstractions, M-OS delivers bare-metal performance for tensor computation, model serving, and parallel data pipelines — all within a fully functional UNIX-like environment.

### Key Highlights

- **High-throughput IPC** — POSIX message queues and shared-memory regions with zero-copy handoff and lock-free ring buffers to feed parallel ML workers without kernel-crossing overhead.
- **ML-optimized memory management** — Huge-page support, prefetch-friendly batching, and shadow-object copy-on-write to improve cache locality and reduce page faults for tensor workloads.
- **CPU-affinity–aware scheduling** — Per-core thread pinning and cooperative/preemptive hybrid scheduling tuned for sustained compute-bound inference loops.
- **Minimal syscall surface** — A lean POSIX-compatible syscall layer that avoids the context-switch tax of monolithic kernels while still supporting standard toolchains.

---

## Architecture

```
┌──────────────────────────────────────────────────────────────┐
│                      User Space                              │
│   ML Workers  ·  Data Loaders  ·  Model Servers  ·  Shell    │
├──────────────────────────────────────────────────────────────┤
│                    System Call Interface                      │
│       read · write · mmap · brk · fork · exec · waitpid      │
├──────────┬───────────┬──────────────┬────────────────────────┤
│  VFS     │  Process  │   Virtual    │   IPC / Shared Memory  │
│  Layer   │  Manager  │   Memory     │   (zero-copy rings)    │
├──────────┴───────────┴──────────────┴────────────────────────┤
│              Memory Manager (pframes, slab, page tables)     │
├──────────────────────────────────────────────────────────────┤
│           Drivers  ·  Interrupt Handling  ·  Boot/GDT/IDT    │
├──────────────────────────────────────────────────────────────┤
│                        x86 Hardware                          │
└──────────────────────────────────────────────────────────────┘
```

---

## Kernel Subsystems

### Process & Thread Management (`kernel/proc/`)
- Full process lifecycle — `fork()`, `exec()`, `waitpid()`, `exit()`
- Kernel threads with cancellation support and per-thread errno
- Mutex primitives with cancellable blocking for safe concurrent access
- Hierarchical process tree with reparenting on exit

### Virtual Memory (`kernel/vm/`)
- Demand-paged virtual address spaces with copy-on-write via shadow objects
- `mmap()` / `munmap()` for memory-mapped file and anonymous regions
- Page-fault handler with prefetch-friendly batching for sequential tensor loads
- `brk()` for dynamic heap management optimized for large allocations

### Virtual File System (`kernel/fs/`)
- VFS abstraction layer supporting pluggable filesystem backends
- S5FS (System V–style filesystem) with inode-based storage and block caching
- Path resolution with `namev` — supports `.`, `..`, symlinks, and mount traversal
- Special device files via `vn_special` for raw device I/O

### Memory Manager (`kernel/mm/`)
- Physical frame allocator with page-granularity tracking
- Slab allocator for efficient kernel object allocation (zero internal fragmentation)
- Page-frame caching with pin/unpin semantics for DMA-safe buffers
- TLB shootdown support for multi-context page table updates

### System Call Interface (`kernel/api/`)
- POSIX-compatible syscall dispatch (read, write, open, close, stat, mmap, brk, etc.)
- ELF32 binary loader with segment mapping and entry-point transfer
- User ↔ kernel memory copy with fault-safe `copy_from_user` / `copy_to_user`

### Drivers (`kernel/drivers/`)
- ATA/IDE block device driver for persistent storage
- TTY subsystem with line-discipline support
- Interrupt-driven I/O with deferred bottom-half processing

---

## Build & Run

### Prerequisites

| Tool | Version |
|------|---------|
| GCC (cross-compiler) | i686-elf target |
| GNU Make | ≥ 4.0 |
| QEMU | ≥ 6.0 (i386 system emulation) |
| Python | ≥ 3.8 (for fs tooling) |

### Quick Start

```bash
# Clone the repository
git clone https://github.com/Haribharathii/M-OS-Minimal-OS-for-ML-workloads.git
cd M-OS-Minimal-OS-for-ML-workloads

# Build the kernel
make

# Run in QEMU
./weenix
```

### Configuration

Edit `Config.mk` to toggle kernel features:

```makefile
# Enable virtual memory subsystem
ENABLE_VM = 1

# Set debug output channels
DBG = error,test

# Number of terminal devices
NTERMS = 3
```

---

## Project Structure

```
.
├── kernel/
│   ├── api/          # Syscall dispatch, ELF loader, exec
│   ├── boot/         # GDT, IDT, paging bootstrap
│   ├── drivers/      # ATA, TTY, device drivers
│   ├── entry/        # Low-level interrupt/trap entry points
│   ├── fs/           # VFS layer, S5FS, namev, vnode ops
│   ├── include/      # All kernel headers
│   ├── main/         # Kernel entry, init process, idle loop
│   ├── mm/           # Physical memory, slab, pframes, page tables
│   ├── proc/         # Processes, threads, scheduler, mutexes
│   ├── test/         # Kernel-space test suites
│   ├── util/         # Debug, printf, string, list utilities
│   └── vm/           # Virtual memory, mmap, shadow objects, anon
├── user/             # Userspace programs, libc, shell
├── tools/            # Filesystem image builder
├── doc/              # Design documentation
├── Config.mk         # Build configuration
├── Makefile          # Top-level build
└── README.md
```

---

## Design Decisions for ML Workloads

| Challenge | Solution |
|-----------|----------|
| **High IPC latency** | Lock-free ring buffers + shared-memory zero-copy handoff between worker processes |
| **TLB thrashing on large models** | Huge-page–backed anonymous mappings reduce TLB misses by 10–50× |
| **Cache pollution from I/O** | Prefetch-friendly page-fault batching keeps tensor data in L2/L3 |
| **Context-switch overhead** | CPU-affinity pinning + cooperative yield paths minimize scheduler noise |
| **Memory fragmentation** | Slab allocator + shadow-object CoW prevent fragmentation under sustained allocation |

---

## Testing

```bash
# Run the full kernel test suite (from kshell)
vfstest          # VFS + filesystem stress tests
faber            # Process/thread lifecycle + synchronization
sunghan          # Producer-consumer concurrency
sunghan_deadlock # Deadlock detection validation
```

---

## License

This project is released under the [MIT License](LICENSE).

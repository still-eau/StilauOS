#!/usr/bin/env bash

set -e

ACTION="${1:-build}"

# --------------------------------------------------
# Configuration
# --------------------------------------------------

CC="clang"
LD="ld.lld"
OBJCOPY="llvm-objcopy"
NASM="nasm"
QEMU="qemu-system-x86_64"

CFLAGS=(
    --target=x86_64-elf
    -ffreestanding
    -fno-stack-protector
    -fno-pic
    -mno-red-zone
    -mno-mmx
    -mno-sse
    -mno-sse2
    -std=c11
    -O2
    -Wall
    -Wextra
    -ISource/Krnl
    -ISource/Drivers
)

LDFLAGS=(
    -T link.ld
)

SRCROOT="Source"
BUILD="build"

BOOT_BIN="$BUILD/boot.bin"
KERNEL_ELF="$BUILD/kernel.elf"
KERNEL_BIN="$BUILD/kernel.bin"
OS_IMG="$BUILD/os.img"

# --------------------------------------------------
# Helpers
# --------------------------------------------------

info() {
    echo -e "\e[36m[BUILD]\e[0m $1"
}

ok() {
    echo -e "\e[32m[ OK ]\e[0m $1"
}

fail() {
    echo -e "\e[31m[ERROR]\e[0m $1"
    exit 1
}

# --------------------------------------------------
# Clean
# --------------------------------------------------

clean_build() {
    info "Cleaning build directory"

    rm -rf "$BUILD"

    ok "Clean done"
}

# --------------------------------------------------
# Build
# --------------------------------------------------

build_os() {
    info "Creating build directory"

    mkdir -p "$BUILD"

    # --------------------------------------------------
    # Bootloader
    # --------------------------------------------------

    info "Assembling boot.asm"

    "$NASM" \
        -f bin \
        "$SRCROOT/Krnl/boot.asm" \
        -o "$BOOT_BIN"

    ok "boot.bin"

    # --------------------------------------------------
    # ASM Objects
    # --------------------------------------------------

    asm_sources=(
        "$SRCROOT/Krnl/boot_entry.asm"
        "$SRCROOT/Krnl/cpu/isr_stubs.asm"
    )

    for src in "${asm_sources[@]}"; do
        name=$(basename "$src" .asm)
        obj="$BUILD/$name.o"

        info "Assembling $src"

        "$NASM" \
            -f elf64 \
            "$src" \
            -o "$obj"
    done

    # --------------------------------------------------
    # C Objects
    # --------------------------------------------------

    c_sources=(
        "$SRCROOT/Krnl/cpu/gdt.c"
        "$SRCROOT/Krnl/cpu/idt.c"
        "$SRCROOT/Krnl/cpu/isr.c"
        "$SRCROOT/Krnl/cpu/cpu.c"
        "$SRCROOT/Krnl/mem/vmm.c"
        "$SRCROOT/Krnl/mem/pmm.c"
        "$SRCROOT/Krnl/mem/krnl_mm.c"
        "$SRCROOT/Krnl/mem/vma.c"
        "$SRCROOT/Drivers/pic.c"
        "$SRCROOT/Drivers/pit.c"
        "$SRCROOT/Drivers/vga.c"
        "$SRCROOT/Drivers/keyboard.c"
        "$SRCROOT/Drivers/ahci.c"
        "$SRCROOT/Drivers/console.c"
        "$SRCROOT/Drivers/mouse.c"
        "$SRCROOT/Krnl/fs/node.c"
        "$SRCROOT/Krnl/fs/path.c"
        "$SRCROOT/Krnl/fs/file.c"
        "$SRCROOT/Krnl/fs/vfs.c"
        "$SRCROOT/Krnl/fs/stilaufs/alloc.c"
        "$SRCROOT/Krnl/fs/stilaufs/inode.c"
        "$SRCROOT/Krnl/fs/stilaufs/super.c"
        "$SRCROOT/Krnl/fs/stilaufs/dir.c"
        "$SRCROOT/Krnl/task/sched.c"
        "$SRCROOT/Krnl/main.c"
    )

    for src in "${c_sources[@]}"; do
        name=$(basename "$src" .c)
        obj="$BUILD/$name.o"

        info "Compiling $src"

        "$CC" \
            "${CFLAGS[@]}" \
            -c \
            "$src" \
            -o \
            "$obj"
    done

    # --------------------------------------------------
    # Link
    # --------------------------------------------------

    objects=(
        "$BUILD/boot_entry.o"
        "$BUILD/isr_stubs.o"
        "$BUILD/gdt.o"
        "$BUILD/idt.o"
        "$BUILD/isr.o"
        "$BUILD/cpu.o"
        "$BUILD/vmm.o"
        "$BUILD/pmm.o"
        "$BUILD/krnl_mm.o"
        "$BUILD/vma.o"
        "$BUILD/pic.o"
        "$BUILD/pit.o"
        "$BUILD/vga.o"
        "$BUILD/keyboard.o"
        "$BUILD/ahci.o"
        "$BUILD/console.o"
        "$BUILD/mouse.o"
        "$BUILD/node.o"
        "$BUILD/path.o"
        "$BUILD/file.o"
        "$BUILD/vfs.o"
        "$BUILD/alloc.o"
        "$BUILD/inode.o"
        "$BUILD/super.o"
        "$BUILD/dir.o"
        "$BUILD/sched.o"
        "$BUILD/main.o"
    )

    info "Linking kernel"

    "$LD" \
        "${LDFLAGS[@]}" \
        "${objects[@]}" \
        -o "$KERNEL_ELF"

    # --------------------------------------------------
    # ELF -> BIN
    # --------------------------------------------------

    info "Creating flat binary"

    "$OBJCOPY" \
        -O binary \
        "$KERNEL_ELF" \
        "$KERNEL_BIN"

    # --------------------------------------------------
    # Disk image
    # --------------------------------------------------

    info "Creating disk image"

    TARGET_SIZE=$((4096 * 512))

    if [[ -f "$OS_IMG" ]]; then
        dd if="$BOOT_BIN" of="$OS_IMG" conv=notrunc status=none
        dd if="$KERNEL_BIN" of="$OS_IMG" seek=1 bs=512 conv=notrunc status=none

        ok "Disk image updated (bootloader and kernel updated, StilauFS partition preserved)"
    else
        cat "$BOOT_BIN" "$KERNEL_BIN" > "$OS_IMG"

        SIZE=$(stat -c%s "$OS_IMG")

        if (( SIZE < TARGET_SIZE )); then
            truncate -s "$TARGET_SIZE" "$OS_IMG"
        fi

        ok "Disk image created ($TARGET_SIZE bytes)"
    fi
}

# --------------------------------------------------
# Run
# --------------------------------------------------

run_os() {
    [[ -f "$OS_IMG" ]] || fail "No image found"

    info "Launching QEMU"

    "$QEMU" \
        -drive "format=raw,file=$OS_IMG" \
        -m 64M \
        -display sdl \
        -serial stdio \
        -no-reboot \
        -no-shutdown
}

# --------------------------------------------------
# Main
# --------------------------------------------------

case "$ACTION" in
    clean)
        clean_build
        ;;
    build)
        build_os
        ;;
    run)
        build_os
        run_os
        ;;
    *)
        fail "Usage: $0 [build|run|clean]"
        ;;
esac
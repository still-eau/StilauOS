param(
    [ValidateSet("build", "run", "clean")]
    [string]$Action = "build"
)

$ErrorActionPreference = "Stop"

# ------------------------------------------------------------------
# Configuration
# ------------------------------------------------------------------

$CC = "clang"
$LD = "ld.lld"
$OBJCOPY = "llvm-objcopy"
$NASM     = "nasm"
$QEMU     = "qemu-system-x86_64"

$CFLAGS = @(
    "--target=x86_64-elf"
    "-ffreestanding"
    "-fno-stack-protector"
    "-fno-pic"
    "-mno-red-zone"
    "-mno-mmx"
    "-mno-sse"
    "-mno-sse2"
    "-std=c11"
    "-O2"
    "-Wall"
    "-Wextra"
    "-ISource/Krnl"
    "-ISource/Drivers"
)

$LDFLAGS = @(
    "-T"
    "link.ld"
    "-nostdlib"
)

$SRCROOT = "Source"
$BUILD   = "build"

$BOOT_BIN   = "$BUILD\boot.bin"
$KERNEL_ELF = "$BUILD\kernel.elf"
$KERNEL_BIN = "$BUILD\kernel.bin"
$OS_IMG     = "$BUILD\os.img"

# ------------------------------------------------------------------
# Helpers
# ------------------------------------------------------------------

function Info($msg) {
    Write-Host "[BUILD] $msg" -ForegroundColor Cyan
}

function Ok($msg) {
    Write-Host "[ OK ] $msg" -ForegroundColor Green
}

function Fail($msg) {
    Write-Host "[ERROR] $msg" -ForegroundColor Red
    exit 1
}

# ------------------------------------------------------------------
# Clean
# ------------------------------------------------------------------

function Clean-Build {

    Info "Cleaning build directory"

    if (Test-Path $BUILD) {
        Remove-Item $BUILD -Recurse -Force
    }

    Ok "Clean done"
}

# ------------------------------------------------------------------
# Build
# ------------------------------------------------------------------

function Build-OS {

    Info "Creating build directory"

    New-Item `
        -ItemType Directory `
        -Force `
        -Path $BUILD | Out-Null

    # --------------------------------------------------------------
    # Bootloader
    # --------------------------------------------------------------

    Info "Assembling boot.asm"

    & $NASM `
        -f bin `
        "$SRCROOT\Krnl\boot.asm" `
        -o $BOOT_BIN

    Ok "boot.bin"

    # --------------------------------------------------------------
    # ASM Objects
    # --------------------------------------------------------------

    $asmSources = @(
        "$SRCROOT\Krnl\boot_entry.asm"
        "$SRCROOT\Krnl\cpu\isr_stubs.asm"
    )

    foreach ($src in $asmSources) {

        $name = [System.IO.Path]::GetFileNameWithoutExtension($src)
        $obj  = "$BUILD\$name.o"

        Info "Assembling $src"

        & $NASM `
            -f elf64 `
            $src `
            -o $obj
    }

    # --------------------------------------------------------------
    # C Objects
    # --------------------------------------------------------------

    $cSources = @(
        "$SRCROOT\Krnl\cpu\gdt.c"
        "$SRCROOT\Krnl\cpu\idt.c"
        "$SRCROOT\Krnl\cpu\isr.c"
        "$SRCROOT\Krnl\cpu\cpu.c"
        "$SRCROOT\Krnl\mem\vmm.c"
        "$SRCROOT\Krnl\mem\pmm.c"
        "$SRCROOT\Krnl\mem\krnl_mm.c"
        "$SRCROOT\Krnl\mem\vma.c"
        "$SRCROOT\Drivers\pic.c"
        "$SRCROOT\Drivers\pit.c"
        "$SRCROOT\Drivers\vga.c"
        "$SRCROOT\Drivers\keyboard.c"
        "$SRCROOT\Drivers\console.c"
        "$SRCROOT\Drivers\mouse.c"
        "$SRCROOT\Krnl\fs\fs.c"
        "$SRCROOT\Krnl\task\sched.c"
        "$SRCROOT\Krnl\main.c"
    )

    foreach ($src in $cSources) {

        $name = [System.IO.Path]::GetFileNameWithoutExtension($src)
        $obj  = "$BUILD\$name.o"

        Info "Compiling $src"

        & $CC `
            $CFLAGS `
            -c `
            $src `
            -o `
            $obj
    }

    # --------------------------------------------------------------
    # Link
    # --------------------------------------------------------------

    $objects = @(
        "$BUILD\boot_entry.o"
        "$BUILD\isr_stubs.o"
        "$BUILD\gdt.o"
        "$BUILD\idt.o"
        "$BUILD\isr.o"
        "$BUILD\cpu.o"
        "$BUILD\vmm.o"
        "$BUILD\pmm.o"
        "$BUILD\krnl_mm.o"
        "$BUILD\vma.o"
        "$BUILD\pic.o"
        "$BUILD\pit.o"
        "$BUILD\vga.o"
        "$BUILD\keyboard.o"
        "$BUILD\console.o"
        "$BUILD\mouse.o"
        "$BUILD\fs.o"
        "$BUILD\sched.o"
        "$BUILD\main.o"
    )

    Info "Linking kernel"

$LDFLAGS = @(
    "-T"
    "link.ld"
)

& $LD `
    $LDFLAGS `
    $objects `
    "-o" `
    $KERNEL_ELF

if ($LASTEXITCODE -ne 0) {
    throw "Link failed"
}

# --------------------------------------------------------------
# ELF -> BIN
# --------------------------------------------------------------

Info "Creating flat binary"

& $OBJCOPY `
    "-O" `
    "binary" `
    $KERNEL_ELF `
    $KERNEL_BIN

    if ($LASTEXITCODE -ne 0) {
        throw "objcopy failed"
    }

    # --------------------------------------------------------------
    # Disk image
    # --------------------------------------------------------------

    Info "Creating disk image"

    $bootBytes   = [IO.File]::ReadAllBytes($BOOT_BIN)
    $kernelBytes = [IO.File]::ReadAllBytes($KERNEL_BIN)
    $targetSize  = 4096 * 512

    if (Test-Path $OS_IMG) {
        # Overwrite bootloader and kernel sectors in place, preserving filesystem space.
        $stream = [IO.File]::OpenWrite($OS_IMG)
        $stream.Write($bootBytes, 0, $bootBytes.Length)
        $stream.Write($kernelBytes, 0, $kernelBytes.Length)
        $stream.Close()
        Ok "Disk image updated (bootloader and kernel updated, StilauFS partition preserved)"
    } else {
        [IO.File]::WriteAllBytes(
            $OS_IMG,
            $bootBytes + $kernelBytes
        )
        $size = (Get-Item $OS_IMG).Length
        if ($size -lt $targetSize) {
            $pad = $targetSize - $size
            $stream = [IO.File]::OpenWrite($OS_IMG)
            $stream.Seek(0, [IO.SeekOrigin]::End) | Out-Null
            $stream.Write(
                (New-Object byte[] $pad),
                0,
                $pad
            )
            $stream.Close()
        }
        Ok "Disk image created ($targetSize bytes)"
    }
}

# ------------------------------------------------------------------
# Run
# ------------------------------------------------------------------

function Run-OS {

    if (-not (Test-Path $OS_IMG)) {
        Fail "No image found"
    }

    Info "Launching QEMU"
    
    & $QEMU `
    -drive "format=raw,file=$OS_IMG" `
    -m 64M `
    -display sdl `
    -serial stdio `
    -no-reboot `
    -no-shutdown
}

# ------------------------------------------------------------------
# Main
# ------------------------------------------------------------------

switch ($Action) {

    "clean" {
        Clean-Build
    }

    "build" {
        Build-OS
    }

    "run" {
        Build-OS
        Run-OS
    }
}
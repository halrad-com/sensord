Add-Type -AssemblyName System.Drawing

function Convert-PngToIco {
    param($pngPath, $icoPath, $sizes)

    $png = [System.Drawing.Image]::FromFile($pngPath)
    $fs = [System.IO.File]::Create($icoPath)
    $bw = New-Object System.IO.BinaryWriter($fs)

    # ICO header
    $bw.Write([Int16]0)
    $bw.Write([Int16]1)
    $bw.Write([Int16]$sizes.Count)

    # Pre-calculate all bitmap data
    $bitmapData = @()
    foreach ($size in $sizes) {
        $bmp = New-Object System.Drawing.Bitmap($size, $size, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
        $g = [System.Drawing.Graphics]::FromImage($bmp)
        $g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
        $g.DrawImage($png, 0, 0, $size, $size)
        $g.Dispose()

        $rect = New-Object System.Drawing.Rectangle(0, 0, $size, $size)
        $bmpData = $bmp.LockBits($rect, [System.Drawing.Imaging.ImageLockMode]::ReadOnly, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
        $bytes = New-Object byte[] ($size * $size * 4)
        [System.Runtime.InteropServices.Marshal]::Copy($bmpData.Scan0, $bytes, 0, $bytes.Length)
        $bmp.UnlockBits($bmpData)

        # Flip vertically for ICO format
        $flipped = New-Object byte[] $bytes.Length
        $rowSize = $size * 4
        for ($y = 0; $y -lt $size; $y++) {
            $srcOffset = $y * $rowSize
            $dstOffset = ($size - 1 - $y) * $rowSize
            [Array]::Copy($bytes, $srcOffset, $flipped, $dstOffset, $rowSize)
        }

        $bitmapData += ,@{
            Size = $size
            Pixels = $flipped
            Bitmap = $bmp
        }
    }

    $offset = 6 + ($sizes.Count * 16)

    foreach ($data in $bitmapData) {
        $size = $data.Size
        $andMaskSize = [Math]::Ceiling($size / 8) * $size
        $andMaskSize = [Math]::Ceiling($andMaskSize / 4) * 4
        $imageSize = 40 + ($size * $size * 4) + $andMaskSize

        $bw.Write([Byte]$size)
        $bw.Write([Byte]$size)
        $bw.Write([Byte]0)
        $bw.Write([Byte]0)
        $bw.Write([Int16]1)
        $bw.Write([Int16]32)
        $bw.Write([Int32]$imageSize)
        $bw.Write([Int32]$offset)
        $offset += $imageSize
    }

    foreach ($data in $bitmapData) {
        $size = $data.Size
        $andMaskSize = [Math]::Ceiling($size / 8) * $size
        $andMaskSize = [Math]::Ceiling($andMaskSize / 4) * 4

        $bw.Write([Int32]40)
        $bw.Write([Int32]$size)
        $bw.Write([Int32]($size*2))
        $bw.Write([Int16]1)
        $bw.Write([Int16]32)
        $bw.Write([Int32]0)
        $bw.Write([Int32]0)
        $bw.Write([Int32]0)
        $bw.Write([Int32]0)
        $bw.Write([Int32]0)
        $bw.Write([Int32]0)

        $bw.Write($data.Pixels)

        $andMask = New-Object byte[] $andMaskSize
        $bw.Write($andMask)

        $data.Bitmap.Dispose()
    }

    $bw.Close()
    $fs.Close()
    $png.Dispose()

    Write-Host "Created $icoPath"
}

$dir = $PSScriptRoot

# App icon (multiple sizes)
Convert-PngToIco (Join-Path $dir "app.png") (Join-Path $dir "app.ico") @(16, 32, 48)

# Tray icons (16x16 and 32x32 for different DPI)
Convert-PngToIco (Join-Path $dir "Rotate-Unlocked-c.png") (Join-Path $dir "unlocked.ico") @(16, 32)
Convert-PngToIco (Join-Path $dir "Rotate-Lock-c.png") (Join-Path $dir "locked.ico") @(16, 32)

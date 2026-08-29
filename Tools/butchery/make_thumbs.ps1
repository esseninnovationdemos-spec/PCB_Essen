# Downscales the showcase renders to gallery thumbnails.
#
# The full-res set is ~4 MB a frame, and an artifact page carries its images
# inline as base64 -- 28 of those would be some 150 MB against a 16 MB ceiling.
# 720 px JPEGs come in around 80 kB each, which fits with room to spare and is
# still sharp on a normal screen. The originals stay on disk; this is only what
# the page shows.

Add-Type -AssemblyName System.Drawing

$SRC = "C:\Users\Metaverse\Documents\PCB_Essen\Tools\butchery\showcase"
$DST = Join-Path $SRC "thumbs"
$WIDTH = 720
$QUALITY = 78

New-Item -ItemType Directory -Force -Path $DST | Out-Null

$codec = [System.Drawing.Imaging.ImageCodecInfo]::GetImageEncoders() |
    Where-Object { $_.MimeType -eq "image/jpeg" }
$params = New-Object System.Drawing.Imaging.EncoderParameters 1
$params.Param[0] = New-Object System.Drawing.Imaging.EncoderParameter(
    [System.Drawing.Imaging.Encoder]::Quality, [int64]$QUALITY)

$done = 0
$bytes = 0
foreach ($file in Get-ChildItem $SRC -Filter "*.png") {
    $src = [System.Drawing.Image]::FromFile($file.FullName)
    $h = [int][math]::Round($WIDTH * $src.Height / $src.Width)

    $bmp = New-Object System.Drawing.Bitmap $WIDTH, $h
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
    $g.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
    $g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::HighQuality
    $g.DrawImage($src, 0, 0, $WIDTH, $h)

    $out = Join-Path $DST ($file.BaseName + ".jpg")
    $bmp.Save($out, $codec, $params)

    $g.Dispose(); $bmp.Dispose(); $src.Dispose()
    $done++
    $bytes += (Get-Item $out).Length
}

"{0} thumbnails, {1:N0} kB total, average {2:N0} kB" -f $done, ($bytes / 1KB), ($bytes / 1KB / [math]::Max($done, 1))

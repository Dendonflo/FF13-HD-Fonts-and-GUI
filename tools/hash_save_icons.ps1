Add-Type -AssemblyName System.Drawing
Add-Type -AssemblyName PresentationCore
Add-Type -AssemblyName WindowsBase

Add-Type @"
using System;
using System.IO;
using System.Runtime.InteropServices;
using System.Drawing;
using System.Drawing.Imaging;
using GdiPixelFormat = System.Drawing.Imaging.PixelFormat;
using WpfPixelFormats = System.Windows.Media.PixelFormats;
using System.Windows.Media.Imaging;

public static class SaveIconHasher {
    const ulong FNV_OFFSET = 14695981039346656037UL;
    const ulong FNV_PRIME  = 1099511628211UL;

    public static ulong HashBytes(byte[] data, ulong h) {
        foreach (byte b in data) {
            h ^= b;
            unchecked { h *= FNV_PRIME; }
        }
        return h;
    }

    public static string HashGdiBitmap(System.Drawing.Bitmap bmp) {
        var rect  = new Rectangle(0, 0, bmp.Width, bmp.Height);
        var bdata = bmp.LockBits(rect, ImageLockMode.ReadOnly, GdiPixelFormat.Format32bppRgb);
        int rowBytes = bmp.Width * 4;
        byte[] row = new byte[rowBytes];
        ulong h = FNV_OFFSET;
        for (int y = 0; y < bmp.Height; y++) {
            Marshal.Copy(bdata.Scan0 + y * bdata.Stride, row, 0, rowBytes);
            h = HashBytes(row, h);
        }
        bmp.UnlockBits(bdata);
        return h.ToString("x16");
    }

    // Hash a file directly (BMP/PNG/etc) without resize
    public static string HashBitmapFile(string path) {
        using (var bmp = (System.Drawing.Bitmap)System.Drawing.Image.FromFile(path))
            return HashGdiBitmap(bmp);
    }

    // Resize via GDI+ with given interpolation
    public static string HashGdiResized(string pngPath, int dstW, int dstH,
                                         System.Drawing.Drawing2D.InterpolationMode interp,
                                         System.Drawing.Drawing2D.PixelOffsetMode pom) {
        using (var src = (System.Drawing.Bitmap)System.Drawing.Image.FromFile(pngPath))
        using (var dst = new System.Drawing.Bitmap(dstW, dstH, GdiPixelFormat.Format32bppRgb))
        using (var g = System.Drawing.Graphics.FromImage(dst)) {
            g.InterpolationMode = interp;
            g.PixelOffsetMode   = pom;
            g.DrawImage(src, 0, 0, dstW, dstH);
            return HashGdiBitmap(dst);
        }
    }

    // Resize via WIC (WPF BitmapDecoder + TransformedBitmap -> FormatConvertedBitmap -> Bgr32)
    public static string HashWIC(string pngPath, int dstW, int dstH) {
        var decoder = BitmapDecoder.Create(new Uri(pngPath, UriKind.Absolute),
                                           BitmapCreateOptions.None, BitmapCacheOption.OnLoad);
        var frame = decoder.Frames[0];

        var scaled = new TransformedBitmap(frame,
            new System.Windows.Media.ScaleTransform(
                (double)dstW / frame.PixelWidth,
                (double)dstH / frame.PixelHeight));

        var conv = new FormatConvertedBitmap(scaled, WpfPixelFormats.Bgr32, null, 0);

        int stride = dstW * 4;
        byte[] pixels = new byte[dstH * stride];
        conv.CopyPixels(new System.Windows.Int32Rect(0, 0, dstW, dstH), pixels, stride, 0);

        ulong h = FNV_OFFSET;
        byte[] row = new byte[stride];
        for (int y = 0; y < dstH; y++) {
            Array.Copy(pixels, y * stride, row, 0, stride);
            h = HashBytes(row, h);
        }
        return h.ToString("x16");
    }
}
"@ -ReferencedAssemblies "System.Drawing","PresentationCore","WindowsBase","System.Xaml"

$png = "C:\Users\dendo\Pictures\temp\SAVE_51.png"
$bmp = "C:\Users\dendo\Pictures\temp\b6ed892446fbc5f2.bmp"
$expected = "b6ed892446fbc5f2"

# Verify hash function
$fromBmp = [SaveIconHasher]::HashBitmapFile($bmp)
Write-Host "Hash of captured BMP : $fromBmp  match=$($fromBmp -eq $expected)"
Write-Host ""

# WIC
$wic = [SaveIconHasher]::HashWIC($png, 512, 256)
$match = if ($wic -eq $expected) { " <<< MATCH" } else { "" }
Write-Host "WIC TransformedBitmap: $wic$match"
Write-Host ""

# GDI+ modes
Write-Host "--- GDI+ modes ---"
$InterpModes = [enum]::GetValues([System.Drawing.Drawing2D.InterpolationMode])
$PomModes    = [enum]::GetValues([System.Drawing.Drawing2D.PixelOffsetMode])
foreach ($im in $InterpModes) {
    foreach ($pm in $PomModes) {
        try {
            $h = [SaveIconHasher]::HashGdiResized($png, 512, 256, $im, $pm)
            $match = if ($h -eq $expected) { " <<< MATCH" } else { "" }
            Write-Host ("  {0,-35} {1,-20} {2}{3}" -f $im, $pm, $h, $match)
        } catch {}
    }
}

# InclinoCar Web Installer

This folder is served as **GitHub Pages** at:
`https://tophe75.github.io/inclinocar/`

## Enabling GitHub Pages

1. Go to your repository on GitHub
2. **Settings → Pages**
3. Under *Source*, select **Deploy from a branch**
4. Branch: `main` · Folder: `/docs`
5. Click **Save**

After ~60 seconds the installer will be live at the URL above.

## How It Works

1. The installer fetches the **latest GitHub Release** via the API
2. Downloads the 6 firmware `.bin` files attached to the release
3. Uses **esptool.js** (Web Serial API) to flash all 3 binaries to the ESP32-C3 at the correct offsets:
   - `0x00000` — bootloader
   - `0x08000` — partition table
   - `0x10000` — application firmware

## Publishing a Release

Push a version tag to trigger the CI build and automatic release:

```bash
git tag v1.0.0
git push origin v1.0.0
```

The GitHub Actions workflow (`.github/workflows/build-release.yml`) will:
1. Build both firmware projects with PlatformIO
2. Collect the 6 `.bin` files
3. Create a GitHub Release with all binaries attached
4. The web installer will automatically serve the new version

## Browser Requirements

Web Serial API requires **Chrome 89+** or **Edge 89+** on desktop.
Firefox and Safari are not supported.

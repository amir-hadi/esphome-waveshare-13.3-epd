## EPD Photo Frame Backend (FastAPI)

Lightweight backend to manage one or more EPD devices:
- Device self-registration and config delivery (default: daily 03:00 wake)
- Immich album integration to pick next image
- 4-bit grayscale conversion and packing for 1600x1200 panel
- HTTP Range support for robust chunked downloads from the device
- SQLite persistence

### Quick start

```bash
# From repo root
cd server
python3 -m venv .venv && source .venv/bin/activate
pip install -r requirements.txt
# Create .env from the keys below
uvicorn app.main:app --host 0.0.0.0 --port 8000 --reload
```

Docker:
```bash
docker build -t epd-backend:latest .
docker run --rm -p 8000:8000 -e SERVER_BASE_URL=http://localhost:8000 \
  -e DB_URL=sqlite+aiosqlite:///./data/epd.db \
  -e DEFAULT_WAKE_TIME=03:00 -e PANEL_WIDTH=1600 -e PANEL_HEIGHT=1200 \
  -e IMMICH_BASE_URL=https://immich.example.com -e IMMICH_API_KEY=changeme \
  -e IMMICH_ALBUM_ID=changeme \
  -v $(pwd)/data:/app/data epd-backend:latest
```

### Environment
- `SERVER_BASE_URL` (default `http://localhost:8000`)
- `DB_URL` (default `sqlite+aiosqlite:///./data/epd.db`)
- `DEFAULT_WAKE_TIME` (default `03:00`)
- `PANEL_WIDTH` (default `1600`)
- `PANEL_HEIGHT` (default `1200`)
- `MIN_DAYS_BEFORE_REPEAT` (default `7`)
- `IMMICH_BASE_URL` (required)
- `IMMICH_API_KEY` (required)
- `IMMICH_ALBUM_ID` (required)

### API overview
- POST `/devices/register`
  - Body: `{ "device_id": "esp32-xxxx", "name": "Kitchen", "location": "DE", "timezone": "Europe/Berlin" }`
  - Upserts device and returns its record
- GET `/devices/{device_id}/config`
  - Returns device config including `image_url` for the EPD to download
- GET `/images/next?device_id=...`
  - Returns nibble-packed 4bpp grayscale image (Range supported)

### Notes
- Output format: two pixels per byte, MS nibble first, row-major order.
- Images are center-cropped to panel aspect then resized and quantized to 4bpp.
- The device should download using HTTP Range in chunks; server returns 206 with Content-Range.

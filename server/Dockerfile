FROM python:3.12-slim

# System deps: ImageMagick for image tooling (optional) and common image libs
RUN apt-get update && apt-get install -y --no-install-recommends \
    imagemagick \
    libjpeg62-turbo \
    libpng16-16 \
  && rm -rf /var/lib/apt/lists/*

WORKDIR /app

COPY server/requirements.txt /app/requirements.txt
RUN pip install --no-cache-dir -r /app/requirements.txt

# App code
COPY server/app /app/app
COPY server/.env.example /app/.env.example

# Data directory for SQLite and cached images
RUN mkdir -p /app/data/images

ENV PYTHONUNBUFFERED=1
EXPOSE 8000
CMD ["uvicorn", "app.main:app", "--host", "0.0.0.0", "--port", "8000"]

import asyncio
import numpy as np
from fastapi import FastAPI, WebSocket, WebSocketDisconnect
from faster_whisper import WhisperModel
import uvicorn

app = FastAPI()

# Limit CPU threads so it doesn't max out all cores and ramp up laptop fans
print("Loading Whisper AI model on Arch Server...")
model = WhisperModel("base", device="cpu", compute_type="int8", cpu_threads=4)
print("Whisper Model Ready!")

# 16kHz, 16-bit Mono PCM: 32,000 bytes = 1.0 second of audio
SAMPLE_RATE = 16000
CHUNK_THRESHOLD_BYTES = 32000 * 2  # Buffer ~2 seconds before transcribing
ENERGY_THRESHOLD = 500  # Silence threshold (RMS amplitude)

def run_whisper_transcription(audio_bytes: bytes) -> str:
    """CPU-bound transcription helper running off the main asyncio event loop."""
    try:
        # Convert int16 PCM bytes to float32 numpy array normalized to [-1.0, 1.0]
        audio_int16 = np.frombuffer(audio_bytes, dtype=np.int16)
        if len(audio_int16) == 0:
            return ""
            
        # Quick silence check: if the room is quiet, skip heavy AI inference!
        rms = np.sqrt(np.mean(audio_int16.astype(np.float64)**2))
        if rms < ENERGY_THRESHOLD:
            return ""

        audio_float32 = audio_int16.astype(np.float32) / 32768.0

        segments, _ = model.transcribe(
            audio_float32,
            beam_size=1,
            language="en",
            vad_filter=True,
            vad_parameters=dict(min_silence_duration_ms=500)
        )
        text = " ".join([segment.text for segment in segments]).strip()
        return text
    except Exception as e:
        print(f"[Whisper Error]: {e}")
        return ""

@app.get("/")
async def root():
    return {"status": "ok", "service": "ESP32 Audio Transcription Server"}

@app.websocket("/ws/audio")
async def websocket_audio_endpoint(websocket: WebSocket):
    await websocket.accept()
    client_host = websocket.client.host if websocket.client else "unknown"
    print(f"[WSS] Client connected from {client_host}")

    audio_buffer = bytearray()

    try:
        while True:
            # Receive binary PCM audio chunks from ESP32
            data = await websocket.receive_bytes()
            if not data:
                continue

            audio_buffer.extend(data)

            # Process buffer when accumulated chunk threshold is reached
            if len(audio_buffer) >= CHUNK_THRESHOLD_BYTES:
                chunk_to_process = bytes(audio_buffer)
                audio_buffer.clear()

                # Offload CPU inference to worker thread so WebSocket ping/pong stays alive
                transcribed_text = await asyncio.to_thread(
                    run_whisper_transcription, chunk_to_process
                )

                if transcribed_text:
                    print(f"[Transcription]: {transcribed_text}")
                    await websocket.send_text(transcribed_text)

    except WebSocketDisconnect:
        print(f"[WSS] Client disconnected: {client_host}")
    except Exception as e:
        print(f"[WSS Error]: {e}")
    finally:
        try:
            await websocket.close()
        except Exception:
            pass

if __name__ == "__main__":
    # Bind explicitly to 0.0.0.0 IPv4 on port 3000
    uvicorn.run(app, host="0.0.0.0", port=3000, log_level="info")

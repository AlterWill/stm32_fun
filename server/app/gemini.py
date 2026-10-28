import json
import re
import threading
import time

from google import genai
from google.genai import types
from pydantic import BaseModel, Field

from .config import settings


class GeminiError(Exception):
    pass


class NoSpeechError(Exception):
    pass


class NoteResult(BaseModel):
    language: str = ""
    transcript: str = ""
    title: str = ""
    summary: str = ""
    key_points: list[str] = Field(default_factory=list)
    tasks: list[str] = Field(default_factory=list)
    tags: list[str] = Field(default_factory=list)


class AskResult(BaseModel):
    language: str = ""
    question: str = ""
    answer: str = ""
    details: str = ""


_client: genai.Client | None = None
_client_lock = threading.Lock()
_non_retryable_markers = (
    "429",
    "resource_exhausted",
    "quota",
    "rate limit",
    "rate_limit",
)
_transient_markers = (
    "500",
    "502",
    "503",
    "504",
    "unavailable",
    "internal",
    "deadline_exceeded",
    "timeout",
    "timed out",
)


def get_client() -> genai.Client:
    global _client
    if not settings.gemini_api_key:
        raise GeminiError("GEMINI_API_KEY is not configured.")
    with _client_lock:
        if _client is None:
            _client = genai.Client(
                api_key=settings.gemini_api_key,
                http_options=types.HttpOptions(
                    timeout=settings.gemini_timeout_seconds * 1000
                ),
            )
    return _client


def parse_json_response(text: str) -> dict:
    cleaned = text.strip()
    cleaned = re.sub(r"^```(?:json)?\s*", "", cleaned)
    cleaned = re.sub(r"\s*```$", "", cleaned)
    try:
        data = json.loads(cleaned)
    except json.JSONDecodeError as exc:
        raise GeminiError("Gemini returned invalid JSON.") from exc
    if not isinstance(data, dict):
        raise GeminiError("Gemini returned an unexpected JSON value.")
    return data


def generate_json(contents, schema: type[BaseModel]) -> BaseModel:
    last_error: Exception | None = None
    for attempt in range(3):
        try:
            response = get_client().models.generate_content(
                model=settings.gemini_model,
                contents=contents,
                config=types.GenerateContentConfig(
                    response_mime_type="application/json",
                    response_schema=schema,
                    temperature=0.2,
                ),
            )
            parsed = getattr(response, "parsed", None)
            if isinstance(parsed, schema):
                return parsed
            if isinstance(parsed, dict):
                return schema.model_validate(parsed)
            text = getattr(response, "text", None)
            if not text:
                raise GeminiError("Gemini returned an empty response.")
            return schema.model_validate(parse_json_response(text))
        except GeminiError:
            raise
        except Exception as exc:
            last_error = exc
            message = str(exc).lower()
            if any(marker in message for marker in _non_retryable_markers):
                break
            if attempt < 2 and any(marker in message for marker in _transient_markers):
                time.sleep(1.5 * (attempt + 1))
                continue
            break
    raise GeminiError(f"Gemini request failed: {str(last_error)[:500]}")


def audio_contents(prompt: str, wav_bytes: bytes) -> list[types.Content]:
    return [
        types.Content(
            role="user",
            parts=[
                types.Part.from_text(text=prompt),
                types.Part.from_bytes(data=wav_bytes, mime_type="audio/wav"),
            ],
        )
    ]


def clean_text(value: str) -> str:
    return " ".join(value.split()).strip()


def clean_list(values: list[str], limit: int) -> list[str]:
    result: list[str] = []
    for value in values:
        cleaned = clean_text(str(value))
        if cleaned and cleaned not in result:
            result.append(cleaned)
        if len(result) >= limit:
            break
    return result


def clamp_text(value: str, limit: int) -> str:
    cleaned = clean_text(value)
    if len(cleaned) <= limit:
        return cleaned
    return cleaned[: limit - 1].rstrip() + "\u2026"


def extract_note(wav_bytes: bytes) -> NoteResult:
    prompt = (
        "You are the transcription and note-extraction engine for a personal "
        "voice-note device. Listen to the attached WAV recording. It is 16 kHz, "
        "16-bit, mono. Auto-detect the spoken language.\n\n"
        "If there is no intelligible speech (silence, noise, or music only), return "
        "an empty transcript and empty title, summary, key_points, tasks, and tags.\n\n"
        "Otherwise produce:\n"
        "- language: the detected language name or BCP-47 code.\n"
        "- transcript: a verbatim transcript in the original language, without commentary.\n"
        "- title: a concise title, at most 80 characters, in the original language.\n"
        "- summary: a useful 2-4 sentence summary in the original language.\n"
        "- key_points: 3-7 short bullet strings in the original language.\n"
        "- tasks: actionable tasks or reminders explicitly mentioned; include spoken "
        "dates or times when present; use an empty list if there are none.\n"
        "- tags: 2-5 short lowercase topical tags.\n\n"
        "Return only JSON matching the provided schema."
    )
    result = generate_json(audio_contents(prompt, wav_bytes), NoteResult)
    result.language = clean_text(result.language)[:32]
    result.transcript = result.transcript.strip()
    result.title = clean_text(result.title)[:200]
    result.summary = result.summary.strip()
    result.key_points = clean_list(result.key_points, 10)
    result.tasks = clean_list(result.tasks, 20)
    result.tags = [tag.lower() for tag in clean_list(result.tags, 10)]
    if not result.transcript:
        raise NoSpeechError()
    return result


def answer_question(wav_bytes: bytes, context_text: str) -> AskResult:
    prompt = (
        "You are a helpful assistant for a personal voice-note device. The attached "
        "WAV is a spoken question. It is 16 kHz, 16-bit, mono. Auto-detect the "
        "spoken language.\n\n"
        "Transcribe the question verbatim in the original language. Then answer it. "
        "Use the recent notes below when they are relevant. If the notes do not "
        "contain the answer, answer from general knowledge and say so only if needed.\n\n"
        "- answer: a direct one-line answer, at most 80 characters, in the same "
        "language as the question. It must fit a tiny screen. No markdown.\n"
        "- details: a fuller 1-3 sentence answer in the same language for the website.\n\n"
        "If there is no intelligible speech, return an empty question and answer.\n\n"
        "RECENT NOTES:\n"
        f"{context_text or '(none)'}\n\n"
        "Return only JSON matching the provided schema."
    )
    result = generate_json(audio_contents(prompt, wav_bytes), AskResult)
    result.language = clean_text(result.language)[:32]
    result.question = result.question.strip()
    result.answer = clamp_text(result.answer, 80)
    result.details = result.details.strip()[:2000] or result.answer
    if not result.question:
        raise NoSpeechError()
    if not result.answer:
        raise GeminiError("Gemini returned an empty answer.")
    return result

from dataclasses import dataclass


@dataclass(frozen=True)
class ErrorSpec:
    code: str
    text: str
    status_code: int


BAD_KEY = ErrorSpec("BAD_KEY", "Bad key", 401)
NO_AUDIO = ErrorSpec("NO_AUDIO", "No audio", 400)
BAD_AUDIO = ErrorSpec("BAD_AUDIO", "Bad audio", 400)
TOO_LONG = ErrorSpec("TOO_LONG", "Too long", 413)
NO_SPEECH = ErrorSpec("NO_SPEECH", "No speech", 422)
NOTE_NOT_FOUND = ErrorSpec("NOTE_NOT_FOUND", "Not found", 404)
NOT_FOUND = ErrorSpec("NOT_FOUND", "Not found", 404)
AI_ERROR = ErrorSpec("AI_ERROR", "AI unavailable", 502)
BAD_REQUEST = ErrorSpec("BAD_REQUEST", "Bad request", 400)
SERVER_ERROR = ErrorSpec("SERVER_ERROR", "Server error", 500)


class DeviceError(Exception):
    def __init__(self, spec: ErrorSpec, detail: str | None = None):
        self.spec = spec
        self.detail = detail
        super().__init__(detail or spec.text)

    @property
    def status_code(self) -> int:
        return self.spec.status_code

    @property
    def code(self) -> str:
        return self.spec.code

    @property
    def display_text(self) -> str:
        return self.spec.text


FAILURE_DISPLAY_TEXT = {
    "NO_SPEECH": "No speech",
    "AI_ERROR": "AI unavailable",
    "INTERRUPTED": "Interrupted",
    "FILE_MISSING": "Audio missing",
    "FAILED": "Failed",
}


def failure_display_text(code: str | None) -> str:
    return FAILURE_DISPLAY_TEXT.get(code or "", "Failed")


for _spec in (
    BAD_KEY,
    NO_AUDIO,
    BAD_AUDIO,
    TOO_LONG,
    NO_SPEECH,
    NOTE_NOT_FOUND,
    NOT_FOUND,
    AI_ERROR,
    BAD_REQUEST,
    SERVER_ERROR,
):
    assert len(_spec.text) <= 16, _spec

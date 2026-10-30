(() => {
  const themeChoices = Array.from(document.querySelectorAll("[data-theme-choice]"));
  const themeKey = "voice-note-theme";

  const applyTheme = (theme, persist = false) => {
    const validTheme = ["light", "auto", "dark"].includes(theme) ? theme : "light";
    document.documentElement.dataset.theme = validTheme;
    themeChoices.forEach((button) => {
      button.setAttribute("aria-pressed", String(button.dataset.themeChoice === validTheme));
    });
    if (persist) {
      try {
        localStorage.setItem(themeKey, validTheme);
      } catch (_) {
        // The page remains usable when storage is unavailable.
      }
    }
  };

  if (themeChoices.length > 0) {
    let savedTheme = "light";
    try {
      savedTheme = localStorage.getItem(themeKey) || "light";
    } catch (_) {
      // Light is the intentional default.
    }
    applyTheme(savedTheme);
    themeChoices.forEach((button) => {
      button.addEventListener("click", () => applyTheme(button.dataset.themeChoice, true));
    });
  }

  const pollers = Array.from(document.querySelectorAll("[data-poll-note]"));
  const deviceActive = document.querySelector("[data-device-active]");
  if (pollers.length === 0 && !deviceActive) {
    return;
  }

  const check = async () => {
    let finished = false;
    for (const element of pollers) {
      const id = element.dataset.pollNote;
      if (!id) {
        continue;
      }
      try {
        const response = await fetch(`/notes/${encodeURIComponent(id)}/status`, {
          headers: { Accept: "application/json" },
          credentials: "same-origin",
        });
        if (!response.ok) {
          continue;
        }
        const data = await response.json();
        if (data.state && data.state !== "processing") {
          finished = true;
        }
      } catch (error) {
        void error;
      }
    }
    if (finished) {
      window.location.reload();
    }
  };

  if (pollers.length > 0) {
    window.setTimeout(check, 1500);
    window.setInterval(check, 4000);
  }
  if (deviceActive) {
    window.setTimeout(() => window.location.reload(), 5000);
  }
})();

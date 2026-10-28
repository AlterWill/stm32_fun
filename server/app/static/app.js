(() => {
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

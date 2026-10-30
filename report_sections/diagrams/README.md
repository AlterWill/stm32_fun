# Mermaid figures for the report

- `system_architecture.mmd`: overall functional block diagram.
- `wiring.mmd`: logical pin-to-pin wiring diagram, not a PCB layout or electrical schematic. Confirm physical connections before use.
- `recording_flow.mmd`: STM32–ESP32 recording and upload flow.
- `server_processing.mmd`: server storage and optional AI processing flow.

Each diagram has been rendered from its Mermaid source as a PDF and a PNG. Use the PDF for LaTeX and the PNG for quick preview. To regenerate with Mermaid CLI, for example:

```sh
mmdc -i system_architecture.mmd -o system_architecture.pdf -b white
```

The Mermaid CLI was installed in a temporary directory for rendering and is not included in this repository. Review each rendered figure for legibility and page fit. In LaTeX, add `\usepackage{graphicx}` to the preamble and place exported PDFs in the report's figure directory. Example:

```latex
\begin{figure}[htbp]
  \centering
  \includegraphics[width=\textwidth]{figures/system_architecture.pdf}
  \caption{Functional block diagram of the smart voice note maker.}
  \label{fig:system-architecture}
\end{figure}
```

The existing Chapter 4 draft already contains TikZ figures for the functional arrangement and flows. Use these Mermaid figures as alternatives or move the architecture and wiring figures into Sections 4.1 and 4.2 to avoid duplicating figures. Mermaid source files cannot be included directly by standard LaTeX; export them first.

# Integration notes

The reference report `mc (1).pdf` was compared with these sections. The chapter and section organization now follows its three-section Chapter 3 and five-section Chapter 5, with numbered assembly and working-principle steps. The project presentation and existing LaTeX template were not present, so compatibility with the template remains to be checked.

Add the following packages to the report preamble if not already loaded:

```latex
\usepackage{booktabs}
\usepackage{tabularx}
\usepackage{graphicx}
\usepackage{tikz}
\usetikzlibrary{arrows.meta,positioning,shapes.geometric}
```

Include `chapter3.tex` as Chapter 3, `chapter4_sections.tex` immediately after Section 4.2, and `chapter5.tex` as Chapter 5. The existing report controls chapter and section numbering; the files do not reset counters. Resolve duplicate labels or package conflicts against the actual template. Figure placeholders compile without image files. Replace them with verified photographs/screenshots and check cross-references after integration. Compile twice to resolve references.

The current repository describes the split firmware as compiled with hardware validation pending. Its current source uses PC6/PC7 for UART and PB10/PB12/PC3 for microphone signals; older migration notes disagree. Confirm the physical assembly and final report's chosen firmware version. No PCB design or test results were supplied.

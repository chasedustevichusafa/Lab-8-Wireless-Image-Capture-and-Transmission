# Repository guidance

## Purpose and layout

This repository stores materials for a KestrelSAT wireless image capture and
transmission lab. It contains documents and experimental assets, not an
application or firmware implementation.

- `Astro 331 Lab 8 C&C Industries.pptx`: Lab 8 proposal and requirements.
- `Lab 4/`: communication experiment spreadsheet, report, XBee datasheet, and
  `.xpro` radio profile.
- `Lab 5/`: camera experiment images, report, and supporting PDF/PNG material.

There are no dependency manifests, build scripts, CI workflows, or automated
tests. Do not invent build commands or claim wireless functionality has been
tested from these assets alone.

## Working with assets

- Preserve original experimental data and images unless the task explicitly
  requests changes. Avoid incidental rewrites of Office documents or radio
  profiles.
- Quote paths in shell commands: many filenames contain spaces, commas, or `&`.
- Keep temporary extracted files and rendered previews outside the checkout.
- Check `git status --short` before and after work; preserve existing changes.
- Use the existing checkout. Cloud tasks are isolated; create a Git worktree only
  if the user requests one.

## Inspection and validation

The prepared cloud environment provides Python 3, Pillow, openpyxl, pdftotext,
and pdftoppm. Confirm availability before using them in another environment.

- Inspect DOCX/PPTX/XLSX and `.xpro` files with Python `zipfile`; verify archive
  integrity with `ZipFile.testzip()` and parse relevant XML. Treat the `.xpro`
  profile as a radio configuration archive, not executable application code.
- Read spreadsheets with `openpyxl.load_workbook(path, read_only=True,
  data_only=True)`. Cached formula values may be absent or stale; openpyxl does
  not calculate formulas.
- Validate JPEG/PNG files with Pillow `Image.open(path).load()`.
- Extract PDF text with `pdftotext`. `Lab 5/Lab 5, Q1.pdf` is image-based;
  an empty text extraction is expected. Render it with `pdftoppm` for inspection.
- After an intentional asset edit, check that the file still opens or renders
  and review the affected content. Binary diffs alone do not establish correctness.

Actual capture, radio programming, transmission, range, and RSSI checks require
the physical KestrelSAT, camera, XBee radios, and external control software.
Report document validation separately from hardware validation.

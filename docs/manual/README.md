# Editor Manual

A user manual for the Adventure RPG Engine editor, written to be readable by both humans
and LLMs. Its purpose: let an AI assistant answer questions like *"how do I make an enemy
chase the player?"* or *"how do I make a quest that counts kills?"* by pointing at concrete
editor steps.

## Contents
- **`index.html`** — the full manual. Open in a browser, or feed the raw HTML to an LLM (it is
  plain semantic HTML with anchored sections and a numbered "How do I…?" recipe list).
- **`llms.txt`** — a compact LLM entry point (the [llms.txt](https://llmstxt.org/) convention):
  a recipe→location map plus key concepts, so an assistant can answer common questions without
  loading the whole HTML, and knows to open `index.html` for detail.
- **`README.md`** — this file.

## Keeping it current
The manual describes editor behavior. The authoritative sources are
`RPG_Engine_Specification.md` and `code_base.md` at the repository root; when they change,
update `index.html` and `llms.txt`. If the manual and the code disagree, the code is correct.

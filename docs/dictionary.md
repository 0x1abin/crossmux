# Dictionary

Look up words and phrases while reading an EPUB using an offline StarDict dictionary stored on the SD card.

## Supported Format

The reader supports **StarDict** dictionaries. When searching for dictionaries online, look for "StarDict format" or files with `.dict`, `.idx`, and `.ifo` extensions.

A dictionary folder must contain:

- `.idx` — word index (required, **must be uncompressed** — a `.idx.gz` will not work; decompress it on your computer with `gzip -d` first)
- `.dict` or `.dict.dz` — definition data (`.dict.dz` is supported as-is; entries are decompressed on the fly during lookup)
- `.ifo` — metadata (optional)

Not supported: `.syn` synonym files (ignored), dictionaries with 64-bit index offsets (`idxoffsetbits=64` in the `.ifo` — rare, and rejected with an error), and HTML-formatted definitions render as raw markup rather than styled text.

> [!NOTE]
> If you use .dict.dz and Crosspoint failed to load your dictionary with error message "Not enough memory", try unzipping it.

## Setting Up a Dictionary

### Download over Wi-Fi

1. Open **Settings → Reader → Manage Dictionaries** and connect to Wi-Fi.
2. Download or update a dictionary. A single download is enabled automatically; **Update All** preserves the current selection.
3. Use **Settings → Reader → Dictionary** to switch between installed dictionaries, or choose **None** to disable lookups.

Online dictionaries are installed under `/.dictionaries/`. Downloads are verified before the old version is replaced, and interrupted installations are recovered the next time the manager opens. Only folders carrying the manager's `.crossmux-resource` marker can be updated or deleted online.

The content profile selected by the current UI language chooses the CrossMux API
at runtime: China uses `crossmux.cn`, Global uses `crossmux.com`. The UI language
is also sent as the `lang` query parameter. Both regions use
`/api/assets/dictionaries/manifest`; third-party configured URLs are unchanged.

### Copy manually

Copy each dictionary folder to `/dictionaries/` on the SD card, e.g. `/dictionaries/webster/webster.idx` + `webster.dict.dz`. A hidden `/.dictionaries/` folder works too. A manual folder whose name conflicts with an online dictionary is never overwritten by the manager.

The Dictionary setting only appears when at least one usable dictionary folder exists. Folders containing more than one dictionary (multiple `.idx` stems) are skipped as ambiguous.

## Looking Up Words and Phrases

Two ways to start a lookup while reading:

- Open the reader menu (**Confirm**) and choose **Look Up**.
- Or set **Settings → Controls → Long-press Menu** to "Dictionary", then hold **Confirm** (~0.4s) on the reading page.

### Selecting Text

**On button-driven devices:**

1. A selection cursor appears on a word. Use **Left/Right** to navigate between words in reading order, and the side **Up/Down** buttons to jump between lines or flip across nearby pages.
2. Press **Confirm** (**Select**) to anchor the start of the selection.
3. **Single word:** Press **Confirm** again immediately (**Look Up**) to look up the highlighted word.
4. **Phrase / multiple words:** Use **Left/Right** or **Up/Down** to extend the highlighted selection across words, lines, or pages. Then press **Confirm** (**Look Up**).
5. Press **Back** at any time to cancel and return to reading.

**On touchscreen devices:**

- Tap or drag the selection handles across words to highlight a word or phrase, then tap **Look Up**.

On the very first lookup with a dictionary (and again if the dictionary file changes), the reader shows *"Indexing may take a while…"* while it builds a small `.qidx` sidecar file next to the `.idx`. Large dictionaries can take longer, and all subsequent lookups use the completed index. The sidecar can be deleted safely at any time — it will simply be rebuilt.

### How Lookup Works

1. **Direct match** — the word or phrase is searched case-insensitively after stripping leading/trailing punctuation and whitespace. Phrases match entries containing spaces, such as idioms and compound terms.
2. **Stemming** — on a miss, common English word forms are retried automatically: possessives and plurals (`dogs` → `dog`, `stories` → `story`) and verb endings (`walked` → `walk`, `running` → `run`, `making` → `make`).
3. **Not found** — a short popup appears and you return to word selection.

## The Definition Screen

When a word is found, the definition screen shows the matched headword at the top and the definition text below, with a page counter for long definitions.

- **Left/Right** or side **Up/Down** — previous / next page
- **Back** — return to word selection
## Where to find dictionaries

> credit to https://github.com/koreader/koreader/wiki/Dictionary-support for the list.

- The [reader.dict](https://www.reader-dict.com) (ex "BoboTiG/ebook-reader-dict") project provides StarDict version of daily dumps of [Wiktionary](https://www.wiktionary.org/) monolingual dictionaries for a variety of languages. It also provides [non-free multilingual](https://www.reader-dict.com) dictionaries.
- The [WikDict](https://www.wikdict.com) project provides free bilingual dictionaries based on [Wiktionary](https://www.wiktionary.org/) for a lot of language pairs. StarDict versions can be [downloaded from here](https://download.wikdict.com/dictionaries/stardict/).
- The [`Vuizur/Wiktionary-Dictionaries`](https://github.com/Vuizur/Wiktionary-Dictionaries) repository contains dictionaries based on [Wiktionary](https://www.wiktionary.org/) from many languages to English, including English-English.
- The [DictInfo](https://www.dictinfo.com/) website provides outdated monolingual dictionaries based on [Wiktionary](https://www.wiktionary.org/).
- The [Firedict site](https://tuxor1337.frama.io/firedict/dictionaries.html) contains a list of freely available dictionaries.
- [wiktionary_stardict](https://xxyzz.github.io/wiktionary_stardict/): update monthly.
- [Fictionaries](https://fictionary.gumroad.com/) provides dictionaries for various speculative fiction books and series.
- [World Factbooks Archive](https://github.com/MilkMp/CIA-World-Factbooks-Archive-1990-2025) provides 36 years of CIA's World Factbook dictionaries in StarDict format.
- [StarDict-Hebrew](https://github.com/Uri-Tauber/StarDict-Hebrew) Hebrew-English StarDict versions of Babylon dictionaries.

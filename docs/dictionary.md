# Dictionary

Look up words and phrases while reading an EPUB using an offline StarDict dictionary stored on the SD card.

## Supported Format

The reader supports **StarDict** dictionaries. When searching for dictionaries online, look for "StarDict format" or files with `.dict`, `.idx`, and `.ifo` extensions.

A dictionary folder must contain:

- `.idx` — word index (required, **must be uncompressed** — a `.idx.gz` will not work; decompress it on your computer with `gzip -d` first)
- `.dict` or `.dict.dz` — definition data (`.dict.dz` is supported as-is; entries are decompressed on the fly during lookup)
- `.ifo` — metadata (optional)

Optional `.syn` files provide authored synonyms and alternate spellings. Dictionaries with 64-bit index offsets (`idxoffsetbits=64` in the `.ifo`) are not supported and are rejected with an error.

> [!NOTE]
> If you use .dict.dz and CrossMux failed to load your dictionary with error message "Not enough memory", try unzipping it.

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

1. **Direct match** — the word or phrase is searched as-is (case-insensitive) in the dictionary index. Leading and trailing punctuation and whitespace are stripped automatically. Multi-word phrases match dictionary entries containing spaces (such as idioms, phrasal verbs, or compound terms). An index may list the same headword more than once (one entry per part of speech, for example); every entry it lists is shown, each labelled with its headword.
2. **Synonyms** — on a miss, if the dictionary ships a `.syn` file, alternate spellings and irregular forms recorded there are resolved to their headword (e.g. `oxen` → `ox`, `colour` → `color`). All distinct definition ranges a synonym points at are collected. If a `.syn` file exists but its `.sidx` sidecar could not be built or read (for example, because of transient low memory), direct matches remain usable, but a lookup that needs synonyms reports an error rather than an incomplete result or a false miss. Indexing is retried the next time the dictionary is opened.
3. **Stemming** — still no match: common English word forms are retried automatically: possessives and plurals (`dogs` → `dog`, `stories` → `story`) and verb endings (`walked` → `walk`, `running` → `run`, `making` → `make`).
4. **Not found** — a short popup appears and you return to word selection.

## The Definition Screen

When a word is found, the definition screen shows the matched headword at the top and the definition text below, with a page counter for long definitions. A word that maps to several definitions shows the complete group one after another, each labelled with its headword. Repeated references to the same data offset and size are shown only once. A single direct definition retains its original presentation. The complete group, including labels and separators, must fit the 64 KiB definition limit and the available memory. If it cannot fit, or any definition cannot be read or decompressed, lookup reports an explicit error; it never presents a truncated definition or a successful prefix of the group.

HTML dictionaries (`sametypesequence=h`) and XDXF dictionaries (`sametypesequence=x`) use the EPUB text-layout engine for supported formatting such as headings, bold, italics, lists, and line breaks. Unrecognized tags keep their text without special formatting. Images and CSS styling are ignored. A complete definition that fits the lookup budget but cannot be parsed or laid out within the styled-rendering budget falls back to plain text. Exceeding the 64 KiB lookup limit is an error, not permission to truncate the result.

Definitions are drawn in your reading font. Dictionaries may use IPA symbols (e.g. `ˈ ː ɹ ɪ ʊ`…) as pronunciation guide, which built-in reader fonts do not include, so they are shown as `�`. To see them, install an [SD card font](./sd-card-fonts.md) that covers IPA and select it as the reading font.

- **Left/Right** or side **Up/Down** — previous / next page
- **Confirm** — select words or a phrase inside the definition, then choose **Look Up**
- **Long press on text** — select the touched word on touchscreen devices when touch reader controls are enabled
- **Back during selection** — cancel selection
- **Back while reading a definition** — return to the previous definition, or to book word selection at the first definition
- **Long Back while reading a definition** — hold **Back** for at least one second, then release to close all definitions and return directly to reading
- **Long press the header Back arrow** — close all definitions and return directly to reading on touchscreen devices, even when touch reader controls are disabled

During definition word selection, press **Back** to cancel selection first, then hold **Back** or long-press the header Back arrow to leave the lookup chain.

Nested lookups use the same dictionary. Both formatted and plain-text definitions support selection.
Returning to a previous definition restores its page and remembers its selection for the next press of **Confirm**.

The reader keeps up to 16 previous definitions. At this limit, another lookup shows a brief message and leaves the current definition open.
A failed lookup reloads the current definition and keeps its page and selection.

The current definition is released before loading another entry, so two definitions do not need to fit in memory together.
Returning to a previous definition reads it from the SD card again.
If a previous definition cannot be read or its position restored, the definition screen closes and returns to book word selection.
The screen also closes if a failed forward lookup cannot reload the current definition.


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

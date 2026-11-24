# Menu languages

Books has no language setting: its menus follow the language Q2 Pod is set
to (System settings → Language, offered by the Languages card app). When
Q2 Pod shows a language, Books looks each menu text up in its own table for
that language, `.books/lang/<code>.tsv`. If there is no Books translation,
it uses shared labels from Q2 Pod's active language pack when the key and
English text both match. Other labels appear in English.

Books ships tables for `de`, `es`, `fr`, `it`, `pl`, `ru`, `uk` and `vi`,
plus core menu labels for `ja`, `ko`, `th`, `zh_CN` and `zh_HK`.
Corrections to the translations are welcome.

The tables use Q2 Pod's language-pack format, and the code is Q2 Pod's
(`de`, `fr`, `zh_CN`...): a first line `# ` and the language's own name,
then one text a line, `key<TAB>English<TAB>translation`, with `\n`, `\t`
and `\\` escaped. Books' key is its English.

- `en.tsv` lists every text Books translates. Copy it to `<code>.tsv`,
  rename the first line and fill in the third column.
- Keep each `%zu`, `%d` or `%s` in the same order: a line whose conversions
  differ from its English is skipped.
- Menu rows are about 13 characters wide; keep translations short.
- A translation with a letter the firmware or Books UI fonts can't draw
  falls back to English.

The release ZIP carries every table here except `en.tsv`.

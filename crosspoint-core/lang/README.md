# Menu languages

Books has no language setting: its menus follow the language Q2 Pod is set
to (System settings → Language, offered by the Languages card app). When
Q2 Pod shows a language, Books looks each menu text up in its own table for
that language, `.books/lang/<code>.tsv`, and shows the English when the
table has no line for it (or there is no table).

Tables here: `de`, `es`, `fr`, `it`, `pl`, `ru`, `uk` (Q2 Pod's European
languages) and `vi`. They were written for Books and haven't been checked
by native speakers yet; corrections are welcome. Q2 Pod's `ja`, `ko`, `th`,
`zh_CN` and `zh_HK` have no table: the menu font has no letters for them.

The tables use Q2 Pod's language-pack format, and the code is Q2 Pod's
(`de`, `fr`, `zh_CN`...): a first line `# ` and the language's own name,
then one text a line, `key<TAB>English<TAB>translation`, with `\n`, `\t`
and `\\` escaped. Books' key is its English.

- `en.tsv` lists every text Books translates. Copy it to `<code>.tsv`,
  rename the first line and fill in the third column.
- Keep each `%zu`, `%d` or `%s` in the same order: a line whose conversions
  differ from its English is skipped.
- Menu rows are about 13 characters wide; keep translations short.
- A translation with a letter the menu font can't draw (CJK, Thai) is shown
  in English for now.

The release ZIP carries every table here except `en.tsv`.

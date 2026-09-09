# Inline code backtick runs

A backtick run whose length differs from the opening run is literal content
(CommonMark §6.4): the opening run of N backticks closes on the first run of
exactly N backticks.

text ` ```lang ``` ` more

A level-2 span with a 3-run inside closes on the matching 2-run:

`` ``` ``` ``

A level-1 span with a 3-run between letters:

`a```b`

Inside a pipe-table cell:

| a | b |
|---|---|
| ` ```x``` ` | cell |

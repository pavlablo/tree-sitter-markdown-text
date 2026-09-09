# HTML block boundary stops a runaway inline delimiter

A CommonMark type-6 HTML block may interrupt a paragraph (CommonMark §4.6), so
an inline delimiter must not cross it. The emphasis below must NOT swallow the
`<div>` as inline content.

*open
<div>
content *close*

The strong variant behaves the same:

**open
<div>
content **close**

A type-7 inline tag (`<span>`) cannot interrupt a paragraph, so the emphasis
may legally span it:

*open
<span>
content *close*

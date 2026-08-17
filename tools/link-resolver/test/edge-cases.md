# link resolver edge cases

## full reference resolved

[full text][ref-target]

[ref-target]: https://full.example.com "Full title"

## full reference NOT resolved

[full text][missing-ref]

## collapsed resolved

[collapsed][]

[collapsed]: https://collapsed.example.com "Collapsed title"

## collapsed NOT resolved

[collapsed-nope][]

## shortcut resolved

[shortcut-hit]

[shortcut-hit]: https://shortcut.example.com

## shortcut NOT resolved

[shortcut-miss]

## reference image resolved

![alt][img-ref]

[img-ref]: https://img.example.com "Img title"

## reference image NOT resolved

![alt][img-miss]

## forward reference: definition after use

[forward-use][fwd]

[fwd]: https://forward.example.com "Fwd title"

## case/whitespace-insensitive matching

[Foo Bar]

[foo   bar]: https://case.example.com "Case title"

## duplicate definition: first wins

[dup]

[dup]: https://first.example.com "First"
[dup]: https://second.example.com "Second"

## inline link is not a reference

[inline only](https://inline.example.com)

## footnote reference is not a shortcut link

see [^1]

[^1]: footnote body

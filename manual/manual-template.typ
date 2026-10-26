// Pandoc -> Typst template: a traditional Game Boy instruction booklet.
//
// Page size follows the real thing: a 105 mm square saddle-stitched
// booklet, warm paper, one brown spot colour, small type and tiny
// screenshots framed like the printed screen shots in a retail manual.
//
// Usage (see `make manual`):
//   pandoc manual/kaartenheld-manual.md --template=manual/manual-template.typ \
//          -t typst -o build/manual.typ
//   typst compile --root . build/manual.typ build/kaartenheld_manual.pdf
//
// The intermediate .typ is written one directory below the repository
// root so the Markdown's `../screenshots/...` image paths resolve there
// exactly as they do from `manual/`.

// Booklet inks: warm white stock, black type and the one spot colour
// every Game Boy manual was printed with (here the logo's red).
#let paper = rgb("#faf5ea")
#let ink = rgb("#1e1a17")
#let brand = rgb("#a3342a")
#let tint = rgb("#f3e1d8")
#let rule = rgb("#c8b7a3")

#set document(title: "KAARTENHELD — Player's Guide")

#set page(
  width: 105mm,
  height: 105mm,
  margin: (x: 9mm, top: 9.5mm, bottom: 12mm),
  fill: paper,
  numbering: "1",
  footer: context [
    #set text(size: 5.4pt, fill: brand, tracking: 0.12em)
    #line(length: 100%, stroke: 0.35pt + rule)
    #v(0.7mm)
    #grid(
      columns: (1fr, auto, 1fr),
      align: (left, center, right),
      [KAARTENHELD],
      [#counter(page).display()],
      [PLAYER'S GUIDE],
    )
  ],
)

#set text(
  font: ("New Computer Modern", "Libertinus Serif", "DejaVu Serif"),
  size: 7.3pt,
  fill: ink,
  lang: "en",
)
#set par(justify: true, leading: 0.66em, spacing: 1.05em)
#set heading(numbering: "1.")

// ── Headings: a numbered chapter rule, then small-caps sections ─────────
#show heading.where(level: 1): it => {
  // Every chapter opens on a fresh booklet page. Weak, so the first
  // chapter after the contents page does not leave a blank behind.
  pagebreak(weak: true)
  block(sticky: true, width: 100%)[
  #v(4mm, weak: true)
  #block(width: 100%)[
    #set text(size: 13pt, weight: "bold", fill: brand, tracking: 0.06em)
    #context [#counter(heading).display(it.numbering)]
    #h(2.4mm)
    #smallcaps(it.body)
    #v(1.4mm)
    #line(length: 100%, stroke: 0.9pt + brand)
  ]
  #v(1.6mm)
  ]
}

#show heading.where(level: 2): it => block(sticky: true, width: 100%)[
  #v(2.2mm)
  #block(width: 100%)[
    #set text(size: 8.6pt, weight: "bold", fill: brand, tracking: 0.04em)
    #smallcaps(it.body)
    #v(0.8mm)
    #line(length: 100%, stroke: 0.3pt + rule)
  ]
  #v(1.2mm)
]

#show heading.where(level: 3): it => {
  v(1.6mm)
  block(width: 100%)[
    #set text(size: 7.6pt, weight: "bold", fill: ink)
    #it.body
  ]
  v(0.6mm)
}

// ── Screenshots: framed like printed screen shots, italic captions ─────
#show figure: it => block(width: 100%, above: 0.8em, below: 1em)[
  #align(center)[
    #block(
      stroke: 0.9pt + ink,
      inset: 1.1mm,
      fill: white,
    )[#set image(width: 38mm)
      #it.body]
  ]
  #v(0.9mm)
  #align(center)[
    #set text(size: 5.6pt, style: "italic", fill: brand)
    #it.caption.body
  ]
]

#show figure.where(kind: table): it => block(width: 100%, above: 0.6em, below: 1.1em)[
  #it.body
  #line(length: 100%, stroke: 0.6pt + brand)
]

// ── Tables: rules top and bottom only, never a grid of boxes ──────────
#set table(
  stroke: none,
  inset: (x: 2.2pt, y: 2.2pt),
  align: horizon,
)
#show table: set text(size: 6.9pt)
#show table: set par(justify: false, leading: 0.55em)

// ── Tip boxes (Markdown blockquotes) ──────────────────────────────────
#show quote.where(block: true): it => block(
  width: 100%,
  fill: tint,
  stroke: (left: 2.2pt + brand),
  inset: (left: 3mm, right: 2.4mm, top: 2mm, bottom: 2mm),
  above: 1.3em,
  below: 1.4em,
)[
  #set text(size: 6.9pt)
  #set par(justify: false, leading: 0.58em)
  #it.body
]

// ── Inline code reads as a printed button / key name ──────────────────
#show raw.where(block: false): it => box(
  fill: tint,
  inset: (x: 1.1pt, y: 0.3pt),
  radius: 0.6pt,
  text(font: "DejaVu Sans Mono", size: 6.4pt, fill: brand, it.text),
)

// ── Lists ─────────────────────────────────────────────────────────────
#set list(indent: 3.6mm, spacing: 0.6em)
#set enum(indent: 3.6mm, spacing: 0.6em)
#show list: set text(size: 7.2pt)

// ── Cover ─────────────────────────────────────────────────────────────
#page(
  numbering: none,
  footer: none,
  margin: (x: 8mm, top: 6mm, bottom: 6mm),
)[
  #v(1mm)
  #align(center)[
    #image("../assets/title-red.png", width: 54mm)
    #v(1.6mm)
    #text(size: 8.2pt, tracking: 0.3em, fill: brand)[PLAYER'S GUIDE]
    #v(1.4mm)
    #line(length: 55%, stroke: 0.6pt + rule)
    #v(1.1mm)
    #text(size: 5.2pt, tracking: 0.3em, fill: brand)[INSTRUCTION BOOKLET]
  ]
  #v(2mm)
  #align(center)[
    #block(stroke: 1.1pt + ink, inset: 1.3mm, fill: white)[
      #image("../screenshots/23-mimic-battle.png", width: 41mm)
    ]
  ]
  #v(2mm)
  #align(center)[
    #text(size: 6.6pt, style: "italic", fill: brand)[
      A card-duelling adventure in five small cards\
      and one big castle.
    ]
  ]
  #v(1fr)
  #align(center)[
    #line(length: 100%, stroke: 0.35pt + rule)
    #v(1.2mm)
    #grid(
      columns: (1fr, auto, 1fr),
      align: (left, bottom, right),
      text(size: 5.2pt, tracking: 0.16em, fill: brand)[DMG-GB-EUR],
      text(size: 6pt, weight: "bold", tracking: 0.22em, fill: ink)[GAME BOY],
      text(size: 5.2pt, tracking: 0.16em, fill: brand)[MADE IN THE GROVE],
    )
  ]
]

// Contents page, in the flat dotted-leader style of a printed booklet.
#page(numbering: none, footer: none)[
  #v(1mm)
  #text(size: 9pt, weight: "bold", tracking: 0.14em, fill: brand)[CONTENTS]
  #v(1.2mm)
  #line(length: 100%, stroke: 0.9pt + brand)
  #v(2.4mm)
  #set text(size: 7pt)
  #set par(leading: 0.62em)
  #show outline.entry: set outline.entry(fill: repeat([.], gap: 1.6pt))
  #outline(title: none, depth: 1, indent: 0pt)
]

// Body pages restart at 1 after the cover.
#counter(page).update(1)

$body$

// ── Back cover ────────────────────────────────────────────────────────
#page(
  numbering: none,
  footer: none,
  margin: (x: 9mm, top: 9mm, bottom: 8mm),
)[
  #align(center)[
    #text(size: 9.5pt, weight: "bold", tracking: 0.14em, fill: brand)[KAARTENHELD]
    #v(0.6mm)
    #text(size: 5.6pt, tracking: 0.18em, fill: brand)[PLAYER'S GUIDE]
    #v(2.4mm)
    #line(length: 100%, stroke: 0.35pt + rule)
  ]
  #v(2.4mm)
  #set text(size: 5.9pt)
  #set par(justify: true, leading: 0.62em, spacing: 0.7em)
  This booklet is the player's companion to *KAARTENHELD*, a homebrew
  card-duelling RPG for the Nintendo Game Boy. It was written for
  heroes who would rather read a page than lose a fight, and printed
  in the traditional pocket format: 105 millimetres square, one spot
  colour, small type, smaller screenshots.

  If a rule in here disagrees with the game, the game is right. If the
  game disagrees with itself, tell the kobold.

  #v(2mm)
  #line(length: 100%, stroke: 0.25pt + rule)
  #v(1.6mm)
  #grid(
    columns: (1fr, auto),
    align: (left + bottom, right + bottom),
    [
      #set text(size: 5.2pt)
      #set par(leading: 0.55em, spacing: 0.4em)
      Printed in the Grove.\
      Not for resale, especially to slimes.\
      © KAARTENHELD
    ],
    [
      // A little printed barcode, for authenticity.
      #let bars = (0.9pt, 0.4pt, 0.7pt, 0.4pt, 0.4pt, 1pt, 0.4pt, 0.7pt, 0.4pt, 0.9pt, 0.4pt, 0.6pt, 0.4pt, 0.4pt, 1pt)
      #stack(
        dir: ltr,
        spacing: 0.5mm,
        ..bars.map(w => rect(width: w, height: 8mm, fill: ink)),
      )
    ],
  )
]

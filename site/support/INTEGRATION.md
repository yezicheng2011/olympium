# Support page integration

This folder is a static page. It does not replace existing download or manual links.

## Deploy

Copy this directory to the site root as `support/`, so the page is `/support/` (or `/support/index.html`).

Included files:

- `index.html`
- `support.css`
- `support.js`
- `address-qr.svg`

Do not point the QR image at an external generator. The SVG is already in this folder.

## Navigation

Add a Support entry without editing the href of a download or manual link:

```html
<a href="/support/">Support</a>
```

On a narrow layout, keep that link outside any menu that is hidden. A second copy in the header's always-visible group is enough:

```html
<a href="/support/">Support</a>
```

Chinese pages can use the same URL. The page has its own English / 中文 switch.

## Address

The only address is `0x53f421bb0192cfcafd9a46a798a377e15c555027`. Do not re-case it. The QR code contains that string and nothing else. The visitor selects Arbitrum One, Ethereum, Base, OP, Polygon, BNB Chain, or Avalanche in their own wallet. The page cannot select it.

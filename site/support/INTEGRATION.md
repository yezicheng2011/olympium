# Support component

A generic static page for a public crypto address. The page is English, with an explicit 中文 switch. It is not branded to one product. Do not deploy this folder from the repository. Copy it by hand when a site is ready.

## Files

Copy this directory to the site web root as `support/`, so the page is `/support/`.

- `index.html`
- `support.css`
- `support.js`
- `address-qr.svg`

`support.js` is the only script, and it is a local file. There is no inline script. The QR image is the local SVG. That matches a content security policy of `script-src 'self'` and `img-src 'self' data:`. Do not point the image at an external QR service. `address-qr.png` is the same code, kept so a scanner test can read it. The page uses the SVG.

## Navigation

Add one link in the header or footer. Do not change an existing download href or an existing manual href.

```html
<a href="/support/">Support</a>
```

On a Chinese site the visible label can be:

```html
<a href="/support/">支持</a>
```

The page still has its own English / 中文 switch. On a narrow layout, put the link where it stays visible.

## Sites

| Site | What to add |
| --- | --- |
| olympium.net, including `/manual/` | A Support link. Leave the Manual link and `/download/v0.1.0/Linkspan-0.1.0.pkg` unchanged. |
| aeroandroid.com | The same local files. Its policy is `script-src 'self'` and `img-src 'self' data:`. |
| wlsaplus.spacehubxyz.hk | A 支持 link only. Leave cache refresh timers disabled. Do not change downloads. |
| mockmini.com | A Support link only. |
| symath.org | A Support link only. |
| quietmouse.org | A Support link only. |
| danielye2011.uk | A 支持 link. The page's own switch covers 中文. |
| danielsproject.uk | A 支持 link on the existing pages, including the five intro pages. Do not rewrite those pages. |

## Address

The only address is `0x53f421bb0192cfcafd9a46a798a377e15c555027`. Do not re-case it. The QR code is that string and nothing else, so it does not encode a chain. The page tells the visitor to select Arbitrum One and a wallet-supported token by hand. No other network is listed. There is no wallet connection, no transaction, and no third-party tracking.

## Manual in this bundle

`manual/index.html` in the deployment zip is the revised Olympium manual. It is for olympium.net only. When that site is updated separately, copy it to `/var/www-de/olympium.net/manual/index.html`. The download href stays `/download/v0.1.0/Linkspan-0.1.0.pkg`. This bundle does not publish a new app package.

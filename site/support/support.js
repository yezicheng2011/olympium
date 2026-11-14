// Copyright 2026 Ye Zicheng
// SPDX-License-Identifier: Apache-2.0
// The address is the exact public string supplied for tips. Do not re-case it.
(function () {
  "use strict";

  var ADDRESS = "0x53f421bb0192cfcafd9a46a798a377e15c555027";

  var STR = {
    en: {
      htmlLang: "en",
      title: "Crypto tips",
      intro: "A public address for optional crypto tips. This page does not connect a wallet and does not send a transaction.",
      addressLabel: "Address",
      copy: "Copy address",
      copied: "Address copied.",
      copyFailed: "Copy failed. Select the address and copy it yourself.",
      qrCaption: "QR code of the address only. It does not encode a chain, a token, or an amount. Select the network and the token in your wallet.",
      networkTitle: "Network",
      networkHelp: "Select Arbitrum One in your wallet before you send. Also select a token your wallet supports on Arbitrum One. The QR code is the address only. It does not choose the network or the token.",
      token: "This address is not for Bitcoin, Solana, or Tron. This page does not claim that every chain or every coin works.",
      notDo: "No wallet connection, seed phrase, or private key. No payment processor, no cash-out to fiat, and no smart contract on this page."
    },
    zh: {
      htmlLang: "zh-Hans",
      title: "加密货币打赏",
      intro: "这是一个可选的公开打赏地址。本页不连接钱包，也不发送交易。",
      addressLabel: "地址",
      copy: "复制地址",
      copied: "地址已复制。",
      copyFailed: "复制失败。请选中地址后自行复制。",
      qrCaption: "二维码只含地址。它不包含链、代币或金额。请在钱包中自行选择网络和代币。",
      networkTitle: "网络",
      networkHelp: "发送前请在钱包中选择 Arbitrum One，并选择你的钱包在 Arbitrum One 上支持的代币。二维码只含地址，不选择网络或代币。",
      token: "这个地址不用于比特币、Solana 或波场。本页不表示每一条链或每一种币都可以。",
      notDo: "本页不连接钱包，不索取助记词或私钥，不经支付机构处理，不兑现法币，也不提供智能合约。"
    }
  };

  var lang = "en";

  function t(key) {
    return STR[lang][key];
  }

  function render() {
    document.documentElement.lang = t("htmlLang");
    document.title = t("title");
    document.getElementById("page-title").textContent = t("title");
    document.getElementById("intro").textContent = t("intro");
    document.getElementById("address-label").textContent = t("addressLabel");
    document.getElementById("copy-button").textContent = t("copy");
    document.getElementById("qr-caption").textContent = t("qrCaption");
    document.getElementById("network-title").textContent = t("networkTitle");
    document.getElementById("network-help").textContent = t("networkHelp");
    document.getElementById("token-note").textContent = t("token");
    document.getElementById("not-do").textContent = t("notDo");
    document.getElementById("copy-status").textContent = "";
    var buttons = document.querySelectorAll("[data-lang]");
    var n;
    for (n = 0; n < buttons.length; n += 1) {
      buttons[n].setAttribute("aria-pressed", buttons[n].getAttribute("data-lang") === lang ? "true" : "false");
    }
  }

  function announce(ok) {
    var live = document.getElementById("copy-status");
    live.textContent = "";
    window.setTimeout(function () {
      live.textContent = ok ? t("copied") : t("copyFailed");
    }, 30);
  }

  function fallbackCopy() {
    var field = document.getElementById("address");
    var range = document.createRange();
    range.selectNodeContents(field);
    var selection = window.getSelection();
    selection.removeAllRanges();
    selection.addRange(range);
    try {
      return document.execCommand("copy");
    } catch (err) {
      return false;
    }
  }

  function copyAddress() {
    if (navigator.clipboard && navigator.clipboard.writeText) {
      navigator.clipboard.writeText(ADDRESS).then(function () {
        announce(true);
      }).catch(function () {
        announce(fallbackCopy());
      });
      return;
    }
    announce(fallbackCopy());
  }

  document.getElementById("qr").setAttribute("data-payload", ADDRESS);
  render();
  document.getElementById("copy-button").addEventListener("click", copyAddress);
  document.getElementById("lang-en").addEventListener("click", function () {
    lang = "en";
    render();
  });
  document.getElementById("lang-zh").addEventListener("click", function () {
    lang = "zh";
    render();
  });
}());

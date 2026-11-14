// Copyright 2026 Ye Zicheng
// SPDX-License-Identifier: Apache-2.0
// The address is the exact public string supplied for tips. Do not re-case it.
(function () {
  "use strict";

  var ADDRESS = "0x53f421bb0192cfcafd9a46a798a377e15c555027";

  var NETWORKS = [
    { id: "arbitrum", en: "Arbitrum One", zh: "Arbitrum One" },
    { id: "ethereum", en: "Ethereum", zh: "Ethereum" },
    { id: "base", en: "Base", zh: "Base" },
    { id: "op", en: "OP", zh: "OP" },
    { id: "polygon", en: "Polygon", zh: "Polygon" },
    { id: "bnb", en: "BNB Chain", zh: "BNB Chain" },
    { id: "avalanche", en: "Avalanche", zh: "Avalanche" }
  ];

  var STR = {
    en: {
      htmlLang: "en",
      title: "Crypto tips",
      intro: "A public address for optional crypto tips. This page does not connect a wallet and does not send a transaction.",
      addressLabel: "Address",
      copy: "Copy address",
      copied: "Address copied.",
      copyFailed: "Copy failed. Select the address and copy it yourself.",
      qrCaption: "QR code of the address only. It does not choose a network, a token, or an amount.",
      networkLegend: "Network to select in your wallet",
      networkHelp: "Arbitrum One is the network this address was received on. The other names are common EVM networks. The address text is the same for each. The QR code does not select a network. In your wallet, choose the network yourself before you send.",
      selected: "Shown network: {name}. Switch to that network in your wallet. This page cannot do it for you.",
      token: "Send only a token your wallet can send on the network you selected. This is an EVM address. It is not a Bitcoin, Solana, or Tron address. It does not support every chain or every coin.",
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
      qrCaption: "二维码只含地址。它不选择网络、代币或金额。",
      networkLegend: "请在钱包中自行选择的网络",
      networkHelp: "此地址是在 Arbitrum One 上收到的。下列其他名称是常见的 EVM 网络。每个网络显示的地址相同。二维码不选择网络。发送前请在钱包里自己切换网络。",
      selected: "当前提示的网络：{name}。请在钱包中切换到该网络。本页无法代为切换。",
      token: "只发送你的钱包在所选网络上支持的代币。这是 EVM 地址，不是比特币、Solana 或波场地址。这里不表示支持每一条链或每一种币。",
      notDo: "本页不连接钱包，不索取助记词或私钥，不经支付机构处理，不兑现法币，也不提供智能合约。"
    }
  };

  var lang = "en";
  var networkId = "arbitrum";

  function t(key) {
    return STR[lang][key];
  }

  function networkName() {
    var i;
    for (i = 0; i < NETWORKS.length; i += 1) {
      if (NETWORKS[i].id === networkId) {
        return NETWORKS[i][lang];
      }
    }
    return NETWORKS[0][lang];
  }

  function render() {
    document.documentElement.lang = t("htmlLang");
    document.title = t("title");
    document.getElementById("page-title").textContent = t("title");
    document.getElementById("intro").textContent = t("intro");
    document.getElementById("address-label").textContent = t("addressLabel");
    document.getElementById("copy-button").textContent = t("copy");
    document.getElementById("qr-caption").textContent = t("qrCaption");
    document.getElementById("network-legend").textContent = t("networkLegend");
    document.getElementById("network-help").textContent = t("networkHelp");
    document.getElementById("token-note").textContent = t("token");
    document.getElementById("not-do").textContent = t("notDo");
    document.getElementById("selected-note").textContent = t("selected").replace("{name}", networkName());
    var buttons = document.querySelectorAll("[data-lang]");
    var n;
    for (n = 0; n < buttons.length; n += 1) {
      buttons[n].setAttribute("aria-pressed", buttons[n].getAttribute("data-lang") === lang ? "true" : "false");
    }
    var nets = document.querySelectorAll("input[name='network']");
    for (n = 0; n < nets.length; n += 1) {
      var id = nets[n].value;
      var label = document.querySelector("label[for='" + nets[n].id + "']");
      var item;
      for (item = 0; item < NETWORKS.length; item += 1) {
        if (NETWORKS[item].id === id) {
          label.textContent = NETWORKS[item][lang];
        }
      }
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

  function buildNetworks() {
    var box = document.getElementById("networks");
    var i;
    for (i = 0; i < NETWORKS.length; i += 1) {
      var id = "network-" + NETWORKS[i].id;
      var input = document.createElement("input");
      input.type = "radio";
      input.name = "network";
      input.id = id;
      input.value = NETWORKS[i].id;
      if (NETWORKS[i].id === "arbitrum") {
        input.checked = true;
      }
      input.addEventListener("change", function (event) {
        networkId = event.target.value;
        render();
      });
      var label = document.createElement("label");
      label.htmlFor = id;
      label.textContent = NETWORKS[i].en;
      box.appendChild(input);
      box.appendChild(label);
    }
  }

  document.getElementById("qr").setAttribute("data-payload", ADDRESS);
  buildNetworks();
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

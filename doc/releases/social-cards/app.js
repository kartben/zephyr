(function () {
  "use strict";

  var CARD_WIDTH = 1200;
  var CARD_HEIGHT = 675;
  var PILOT_SLUGS = new Set(["wireguard-vpn", "html-dashboard"]);
  var ASSET_PATHS = {
    data: "./data/zephyr-4.4.json",
    iconDir: "./icons",
    logo: "../../_static/images/logo.svg"
  };

  var THEME = {
    backgroundTop: "#171b1e",
    backgroundBottom: "#111416",
    shell: "#1c2227",
    shellStroke: "#ffffff14",
    surface: "#21282d",
    surfaceStroke: "#ffffff12",
    panel: "#11161a",
    panelStroke: "#ffffff16",
    textMain: "#f4f7f8",
    textMuted: "#c7d0d6",
    textSubtle: "#95a1aa",
    lineSoft: "#ffffff10",
    lineDim: "#ffffff0d",
    darkBorder: "#0d1114"
  };

  var ACCENTS = {
    blue: {
      main: "#0070C5",
      soft: "#338DD1",
      glow: "#66A9DC",
      pale: "#B1E4FA"
    },
    sky: {
      main: "#00AEFF",
      soft: "#33BEF2",
      glow: "#66CEF5",
      pale: "#B1E4FA"
    },
    violet: {
      main: "#9454DB",
      soft: "#AF7FE4",
      glow: "#7929D2",
      pale: "#D6C0F2"
    },
    indigo: {
      main: "#7929D2",
      soft: "#9454DB",
      glow: "#AF7FE4",
      pale: "#D6C0F2"
    }
  };

  function escapeXml(value) {
    return String(value)
      .replace(/&/g, "&amp;")
      .replace(/</g, "&lt;")
      .replace(/>/g, "&gt;")
      .replace(/"/g, "&quot;")
      .replace(/'/g, "&apos;");
  }

  function escapeHtml(value) {
    return escapeXml(value);
  }

  function escapeRegExp(value) {
    return value.replace(/[.*+?^${}()|[\]\\]/g, "\\$&");
  }

  function slugify(value) {
    return String(value).toLowerCase().replace(/[^a-z0-9-]+/g, "-").replace(/^-+|-+$/g, "");
  }

  function normalizeCard(card) {
    return {
      slug: card.slug,
      countdown: card.countdown,
      title: card.title,
      tagline: card.tagline,
      linkedin_copy: card.linkedin_copy,
      accent: card.accent,
      icon: card.icon,
      source_ref: Array.isArray(card.source_ref) ? card.source_ref : [card.source_ref].filter(Boolean)
    };
  }

  function estimateTextWidth(text, fontSize, bold) {
    var units = 0;

    for (var i = 0; i < text.length; i += 1) {
      var ch = text[i];

      if (ch === " ") {
        units += 0.32;
      } else if (/[mwMW@#%&]/.test(ch)) {
        units += 0.88;
      } else if (/[A-Z0-9]/.test(ch)) {
        units += 0.65;
      } else if (/[ilI1,.:;|`']/.test(ch)) {
        units += 0.28;
      } else if (/[-/()]/.test(ch)) {
        units += 0.38;
      } else {
        units += 0.54;
      }
    }

    return units * fontSize * (bold ? 1.03 : 1);
  }

  function wrapText(text, maxWidth, fontSize, bold, maxLines) {
    var words = String(text).trim().split(/\s+/);
    var lines = [];
    var current = "";

    for (var i = 0; i < words.length; i += 1) {
      var candidate = current ? current + " " + words[i] : words[i];

      if (estimateTextWidth(candidate, fontSize, bold) <= maxWidth || !current) {
        current = candidate;
        continue;
      }

      lines.push(current);
      current = words[i];
    }

    if (current) {
      lines.push(current);
    }

    if (lines.length <= maxLines) {
      return lines;
    }

    var trimmed = lines.slice(0, maxLines);
    var last = trimmed[trimmed.length - 1];

    while (last.length > 0 && estimateTextWidth(last + "…", fontSize, bold) > maxWidth) {
      last = last.slice(0, -1);
    }

    trimmed[trimmed.length - 1] = last.replace(/[ ,;:.-]+$/g, "") + "…";
    return trimmed;
  }

  function renderTextLines(lines, x, y, lineHeight, fontSize, fontWeight, fill, letterSpacing) {
    return [
      '<text x="' + x + '" y="' + y + '" fill="' + fill + '" font-family="Clear Sans, sans-serif" font-size="' + fontSize + '" font-weight="' + fontWeight + '"' +
      (letterSpacing ? ' letter-spacing="' + letterSpacing + '"' : "") +
      ">"
    ].concat(lines.map(function (line, index) {
      return '<tspan x="' + x + '" dy="' + (index === 0 ? 0 : lineHeight) + '">' + escapeXml(line) + "</tspan>";
    })).concat(["</text>"]).join("");
  }

  function parseSvgAsset(svgText) {
    var viewBoxMatch = svgText.match(/viewBox="([^"]+)"/i);
    var inner = svgText
      .replace(/^[\s\S]*?<svg[^>]*>/i, "")
      .replace(/<\/svg>\s*$/i, "")
      .replace(/<metadata[\s\S]*?<\/metadata>/i, "")
      .trim();
    var ids = [];
    var idRegex = /id="([^"]+)"/g;
    var match;

    while ((match = idRegex.exec(svgText)) !== null) {
      ids.push(match[1]);
    }

    return {
      viewBox: viewBoxMatch ? viewBoxMatch[1] : "0 0 240 240",
      inner: inner,
      ids: ids
    };
  }

  function namespaceSvg(asset, prefix) {
    var inner = asset.inner;

    asset.ids.forEach(function (id) {
      var scoped = prefix + "-" + id;
      inner = inner.replace(new RegExp('id="' + escapeRegExp(id) + '"', "g"), 'id="' + scoped + '"');
      inner = inner.replace(new RegExp("url\\(#" + escapeRegExp(id) + "\\)", "g"), "url(#" + scoped + ")");
      inner = inner.replace(new RegExp('xlink:href="#' + escapeRegExp(id) + '"', "g"), 'xlink:href="#' + scoped + '"');
      inner = inner.replace(new RegExp('href="#' + escapeRegExp(id) + '"', "g"), 'href="#' + scoped + '"');
    });

    return {
      viewBox: asset.viewBox,
      inner: inner
    };
  }

  function parseViewBox(viewBox) {
    return viewBox.split(/\s+/).map(function (value) { return Number(value); });
  }

  function fitAsset(viewBox, maxWidth, maxHeight) {
    var box = parseViewBox(viewBox);
    var width = box[2];
    var height = box[3];
    var scale = Math.min(maxWidth / width, maxHeight / height);

    return {
      width: width * scale,
      height: height * scale,
      xOffset: -box[0] * scale,
      yOffset: -box[1] * scale,
      scale: scale
    };
  }

  function renderGrid(x, y, width, height) {
    var lines = [];
    var columns = 6;
    var rows = 7;

    for (var i = 1; i < columns; i += 1) {
      var vx = x + (width / columns) * i;
      lines.push('<line x1="' + vx.toFixed(2) + '" y1="' + y + '" x2="' + vx.toFixed(2) + '" y2="' + (y + height) + '" stroke="' + THEME.lineDim + '" stroke-width="1"/>');
    }

    for (var j = 1; j < rows; j += 1) {
      var hy = y + (height / rows) * j;
      lines.push('<line x1="' + x + '" y1="' + hy.toFixed(2) + '" x2="' + (x + width) + '" y2="' + hy.toFixed(2) + '" stroke="' + THEME.lineDim + '" stroke-width="1"/>');
    }

    return lines.join("");
  }

  function renderHtmlDashboardIllustration(cardId, accent, assets) {
    var logoAsset = namespaceSvg(assets.logo, cardId + "-dashboard-logo");
    var logoFit = fitAsset(logoAsset.viewBox, 64, 24);
    var clipId = cardId + "-dashboard-clip";

    return [
      '<g transform="translate(36 58)">',
      "<defs>",
      '<clipPath id="' + clipId + '">',
      '<rect width="332" height="256" rx="18"/>',
      "</clipPath>",
      "</defs>",
      '<g clip-path="url(#' + clipId + ')">',
      '<rect width="332" height="256" fill="#f5f7fa"/>',
      '<rect width="332" height="28" rx="18" fill="#212734"/>',
      '<rect x="0" y="14" width="332" height="14" fill="#212734"/>',
      '<circle cx="18" cy="14" r="4.5" fill="#ff6158"/>',
      '<circle cx="34" cy="14" r="4.5" fill="#ffbd2e"/>',
      '<circle cx="50" cy="14" r="4.5" fill="#27c93f"/>',
      '<rect x="108" y="7" width="118" height="14" rx="7" fill="#2c3444"/>',
      '<g transform="translate(0 28)">',
      '<rect width="94" height="228" fill="#3c4a75"/>',
      '<g transform="translate(14 14) scale(' + logoFit.scale.toFixed(6) + ')">' + logoAsset.inner + "</g>",
      '<line x1="14" y1="44" x2="80" y2="44" stroke="#8696b7" stroke-width="1"/>',
      '<text x="14" y="64" fill="#d7a7ff" font-family="Clear Sans, sans-serif" font-size="8.5" font-weight="700" letter-spacing="0.12em">BUILD REPORT</text>',
      '<rect x="14" y="78" width="66" height="22" rx="6" fill="#26345d"/>',
      '<rect x="20" y="86" width="28" height="4" rx="2" fill="#d5e7ff"/>',
      '<rect x="14" y="112" width="42" height="4" rx="2" fill="#c3d7f2"/>',
      '<rect x="14" y="130" width="38" height="4" rx="2" fill="#c3d7f2"/>',
      '<rect x="14" y="148" width="34" height="4" rx="2" fill="#c3d7f2"/>',
      '<rect x="14" y="166" width="28" height="4" rx="2" fill="#c3d7f2"/>',
      '<rect x="94" width="238" height="228" fill="#f7f8fa"/>',
      '<text x="112" y="28" fill="#31363c" font-family="Clear Sans, sans-serif" font-size="15" font-weight="700">Build Summary</text>',
      '<line x1="112" y1="36" x2="238" y2="36" stroke="#9bb8d4" stroke-width="3"/>',
      '<rect x="112" y="52" width="146" height="108" rx="8" fill="#ffffff" stroke="#d4dde6" stroke-width="1"/>',
      '<rect x="266" y="52" width="52" height="108" rx="8" fill="#ffffff" stroke="#d4dde6" stroke-width="1"/>',
      '<rect x="112" y="168" width="206" height="42" rx="8" fill="#ffffff" stroke="#d4dde6" stroke-width="1"/>',
      '<rect x="122" y="68" width="50" height="5" rx="2.5" fill="#4b5159"/>',
      '<rect x="122" y="82" width="34" height="4" rx="2" fill="#30353b"/>',
      '<rect x="122" y="95" width="48" height="4" rx="2" fill="#30353b"/>',
      '<rect x="122" y="111" width="60" height="4" rx="2" fill="#30353b"/>',
      '<rect x="122" y="123" width="78" height="10" rx="5" fill="#fff0f8" stroke="#f1c7dd" stroke-width="1"/>',
      '<rect x="122" y="174" width="66" height="5" rx="2.5" fill="#4b5159"/>',
      '<rect x="122" y="187" width="108" height="10" rx="5" fill="#fff0f8" stroke="#f1c7dd" stroke-width="1"/>',
      '<rect x="276" y="68" width="24" height="5" rx="2.5" fill="#4b5159"/>',
      '<rect x="276" y="82" width="16" height="4" rx="2" fill="#30353b"/>',
      '<rect x="276" y="99" width="20" height="4" rx="2" fill="#30353b"/>',
      '<rect x="276" y="116" width="14" height="4" rx="2" fill="#30353b"/>',
      '<rect x="276" y="133" width="18" height="4" rx="2" fill="#30353b"/>',
      "</g>",
      "</g>",
      '<rect x="0" y="0" width="332" height="256" rx="18" fill="none" stroke="#dde5ec" stroke-width="1.5"/>',
      '<rect x="0" y="0" width="332" height="256" rx="18" fill="none" stroke="' + accent.soft + '" stroke-opacity="0.12" stroke-width="1.5"/>',
      "</g>"
    ].join("");
  }

  function renderScopeCleanupIllustration(cardId, accent) {
    var clipId = cardId + "-cleanup-clip";
    var monoFamily = "Menlo, Consolas, monospace";

    return [
      '<g transform="translate(34 82)">',
      "<defs>",
      '<clipPath id="' + clipId + '">',
      '<rect width="336" height="228" rx="18"/>',
      "</clipPath>",
      "</defs>",
      '<rect width="336" height="228" rx="18" fill="#161b21" stroke="' + accent.soft + '" stroke-opacity="0.24" stroke-width="1.5"/>',
      '<rect width="336" height="30" rx="18" fill="#202735"/>',
      '<rect x="0" y="14" width="336" height="16" fill="#202735"/>',
      '<circle cx="18" cy="15" r="4" fill="#ff6158"/>',
      '<circle cx="34" cy="15" r="4" fill="#ffbd2e"/>',
      '<circle cx="50" cy="15" r="4" fill="#27c93f"/>',
      '<rect x="216" y="8" width="96" height="14" rx="7" fill="#2b3344"/>',
      '<g clip-path="url(#' + clipId + ')">',
      '<rect x="0" y="30" width="336" height="198" fill="#161b21"/>',
      '<rect x="22" y="48" width="220" height="28" rx="10" fill="' + accent.main + '" fill-opacity="0.10"/>',
      '<rect x="22" y="130" width="250" height="28" rx="10" fill="' + accent.main + '" fill-opacity="0.08"/>',
      '<text x="28" y="67" fill="' + accent.pale + '" font-family="' + monoFamily + '" font-size="17" font-weight="700">scope_guard(k_mutex)(&amp;lock);</text>',
      '<text x="28" y="98" fill="#e2e8ee" font-family="' + monoFamily + '" font-size="17">if (error_condition) {</text>',
      '<text x="28" y="129" fill="#ffffff" font-family="' + monoFamily + '" font-size="17" font-weight="700">    return;</text>',
      '<text x="144" y="129" fill="#7fb9a4" font-family="' + monoFamily + '" font-size="14">// unlocks here</text>',
      '<text x="28" y="160" fill="#e2e8ee" font-family="' + monoFamily + '" font-size="17">}</text>',
      '<text x="28" y="197" fill="#7fb9a4" font-family="' + monoFamily + '" font-size="14">// unlocks here too!</text>',
      "</g>",
      '<rect x="0" y="0" width="336" height="228" rx="18" fill="none" stroke="#c7d2dd" stroke-opacity="0.10" stroke-width="1"/>',
      "</g>"
    ].join("");
  }

  function renderWireGuardIllustration(cardId, assets) {
    var officialAsset = namespaceSvg(assets.wireguardOfficial, cardId + "-wireguard-official");
    var dragonWidth = 302;
    var dragonHeight = 300;
    var centerX = 202;
    var centerY = 219;
    var scale = Math.min(252 / dragonWidth, 252 / dragonHeight);
    var drawWidth = dragonWidth * scale;
    var drawHeight = dragonHeight * scale;
    var x = centerX - (drawWidth / 2);
    var y = centerY - (drawHeight / 2);
    var clipId = cardId + "-wireguard-dragon-clip";

    return [
      "<defs>",
      '<clipPath id="' + clipId + '">',
      '<rect width="' + dragonWidth + '" height="' + dragonHeight + '"/>',
      "</clipPath>",
      "</defs>",
      '<circle cx="' + centerX + '" cy="' + centerY + '" r="130" fill="#ffffff" fill-opacity="0.035"/>',
      '<circle cx="' + centerX + '" cy="' + centerY + '" r="130" fill="none" stroke="#88171a" stroke-opacity="0.18" stroke-width="1.5"/>',
      '<g transform="translate(' + x.toFixed(2) + " " + y.toFixed(2) + ') scale(' + scale.toFixed(6) + ')">',
      '<g clip-path="url(#' + clipId + ')">' + officialAsset.inner + "</g>",
      "</g>"
    ].join("");
  }

  function filenameForCard(card) {
    return slugify(card.countdown) + "-" + card.slug + ".svg";
  }

  function createCardSvg(card, assets) {
    var accent = ACCENTS[card.accent] || ACCENTS.blue;
    var cardId = card.slug;
    var leftX = 72;
    var textWidth = 520;
    var rightPanelX = 724;
    var rightPanelY = 118;
    var rightPanelWidth = 404;
    var rightPanelHeight = 438;
    var rightPanelCenterX = rightPanelX + rightPanelWidth / 2;
    var rightPanelCenterY = rightPanelY + rightPanelHeight / 2;
    var titleSize;
    if (card.title.length > 52) {
      titleSize = 44;
    } else if (card.title.length > 40) {
      titleSize = 48;
    } else if (card.title.length > 32) {
      titleSize = 52;
    } else {
      titleSize = 58;
    }
    var taglineSize = card.tagline.length > 102 ? 26 : 28;
    var titleLines = wrapText(card.title, textWidth, titleSize, true, 4);
    var taglineLines = wrapText(card.tagline, textWidth, taglineSize, false, 4);
    var titleLineHeight = Math.round(titleSize * 1.1);
    var taglineLineHeight = Math.round(taglineSize * 1.34);
    var titleY = 218;
    var taglineY = titleY + ((titleLines.length - 1) * titleLineHeight) + 70;
    var logoAsset = namespaceSvg(assets.logo, cardId + "-logo");
    var logoFit = fitAsset(logoAsset.viewBox, 184, 58);
    var logoX = 72;
    var logoY = 536;
    var illustration;
    var trademarkNote = "";

    if (card.slug === "html-dashboard") {
      illustration = '<g transform="translate(' + rightPanelX + " " + rightPanelY + ')">' + renderHtmlDashboardIllustration(cardId, accent, assets) + "</g>";
    } else if (card.slug === "wireguard-vpn") {
      illustration = '<g transform="translate(' + rightPanelX + " " + rightPanelY + ')">' + renderWireGuardIllustration(cardId, assets) + "</g>";
      trademarkNote = [
        '<text x="1112" y="596" text-anchor="end" fill="' + THEME.textSubtle + '" font-family="Clear Sans, sans-serif" font-size="10.5">',
        '<tspan x="1112" dy="0">"WireGuard" and the "WireGuard" logo are</tspan>',
        '<tspan x="1112" dy="13">registered trademarks of Jason A. Donenfeld.</tspan>',
        "</text>"
      ].join("");
    } else if (card.slug === "scope-based-cleanup-helpers") {
      illustration = '<g transform="translate(' + rightPanelX + " " + rightPanelY + ')">' + renderScopeCleanupIllustration(cardId, accent) + "</g>";
    } else {
      var iconAsset = namespaceSvg(assets.icons[card.icon], cardId + "-icon");
      var iconFit = fitAsset(iconAsset.viewBox, 246, 246);
      var iconTranslateX = rightPanelCenterX - (iconFit.width / 2) + iconFit.xOffset;
      var iconTranslateY = rightPanelCenterY - (iconFit.height / 2) + iconFit.yOffset;

      illustration = '<g transform="translate(' + iconTranslateX.toFixed(2) + " " + iconTranslateY.toFixed(2) + ') scale(' + iconFit.scale.toFixed(6) + ')" fill="currentColor" style="color:' + accent.pale + ';">' + iconAsset.inner + "</g>";
    }

    return [
      '<svg xmlns="http://www.w3.org/2000/svg" xmlns:xlink="http://www.w3.org/1999/xlink" viewBox="0 0 ' + CARD_WIDTH + " " + CARD_HEIGHT + '" role="img" aria-labelledby="' + cardId + '-title ' + cardId + '-desc">',
      "<title id=\"" + cardId + '-title">' + escapeXml(card.countdown + " " + card.title) + "</title>",
      "<desc id=\"" + cardId + '-desc">' + escapeXml(card.tagline) + "</desc>",
      "<defs>",
      '<linearGradient id="' + cardId + '-bg" x1="0" y1="0" x2="1" y2="1">',
      '<stop offset="0%" stop-color="' + THEME.backgroundTop + '"/>',
      '<stop offset="100%" stop-color="' + THEME.backgroundBottom + '"/>',
      "</linearGradient>",
      '<linearGradient id="' + cardId + '-accent-band" x1="0" y1="0" x2="1" y2="0">',
      '<stop offset="0%" stop-color="' + accent.main + '"/>',
      '<stop offset="100%" stop-color="' + accent.soft + '"/>',
      "</linearGradient>",
      '<radialGradient id="' + cardId + '-halo" cx="50%" cy="50%" r="60%">',
      '<stop offset="0%" stop-color="' + accent.glow + '" stop-opacity="0.24"/>',
      '<stop offset="62%" stop-color="' + accent.soft + '" stop-opacity="0.08"/>',
      '<stop offset="100%" stop-color="' + accent.main + '" stop-opacity="0"/>',
      "</radialGradient>",
      "</defs>",
      '<rect width="' + CARD_WIDTH + '" height="' + CARD_HEIGHT + '" fill="url(#' + cardId + '-bg)"/>',
      '<rect x="18" y="18" width="1164" height="639" rx="30" fill="' + THEME.shell + '" stroke="' + THEME.shellStroke + '" stroke-width="1.5"/>',
      '<rect x="42" y="42" width="1116" height="591" rx="24" fill="' + THEME.surface + '" stroke="' + THEME.surfaceStroke + '" stroke-width="1.25"/>',
      '<rect x="42" y="42" width="1116" height="6" rx="3" fill="url(#' + cardId + '-accent-band)"/>',
      '<line x1="678" y1="120" x2="678" y2="556" stroke="' + THEME.lineSoft + '" stroke-width="1"/>',
      '<g transform="translate(72 58)">',
      // '<rect width="110" height="42" rx="21" fill="' + accent.main + '" fill-opacity="0.14" stroke="' + accent.soft + '" stroke-width="1.25"/>',
      // '<text x="55" y="27" text-anchor="middle" fill="' + accent.pale + '" font-family="Clear Sans, sans-serif" font-size="18" font-weight="700" letter-spacing="0.08em">' + escapeXml(card.countdown) + "</text>",
      "</g>",
      '<text x="' + leftX + '" y="148" fill="' + THEME.textSubtle + '" font-family="Clear Sans, sans-serif" font-size="17" font-weight="700" letter-spacing="0.16em">ZEPHYR RTOS 4.4 IS HERE!</text>',
      renderTextLines(titleLines, leftX, titleY, titleLineHeight, titleSize, 700, THEME.textMain),
      renderTextLines(taglineLines, leftX, taglineY, taglineLineHeight, taglineSize, 400, THEME.textMuted),
      '<g opacity="0.78" transform="translate(' + logoX.toFixed(2) + " " + logoY.toFixed(2) + ') scale(' + logoFit.scale.toFixed(6) + ')">' + logoAsset.inner + "</g>",
      '<g transform="translate(' + rightPanelX + " " + rightPanelY + ')">',
      '<rect width="' + rightPanelWidth + '" height="' + rightPanelHeight + '" rx="28" fill="' + THEME.panel + '" stroke="' + accent.soft + '" stroke-opacity="0.2" stroke-width="1.5"/>',
      renderGrid(24, 24, rightPanelWidth - 48, rightPanelHeight - 48),
      '<circle cx="' + (rightPanelWidth / 2) + '" cy="' + (rightPanelHeight / 2) + '" r="146" fill="url(#' + cardId + '-halo)"/>',
      '<rect x="18" y="18" width="' + (rightPanelWidth - 36) + '" height="' + (rightPanelHeight - 36) + '" rx="22" fill="none" stroke="' + THEME.lineSoft + '" stroke-width="1"/>',
      '<path d="M26 66h62" stroke="' + accent.main + '" stroke-opacity="0.24" stroke-width="6" stroke-linecap="round"/>',
      '<path d="M316 66h62" stroke="' + accent.main + '" stroke-opacity="0.24" stroke-width="6" stroke-linecap="round"/>',
      '<path d="M26 372h62" stroke="' + accent.main + '" stroke-opacity="0.24" stroke-width="6" stroke-linecap="round"/>',
      '<path d="M316 372h62" stroke="' + accent.main + '" stroke-opacity="0.24" stroke-width="6" stroke-linecap="round"/>',
      "</g>",
      illustration,
      trademarkNote,
      "</svg>"
    ].join("");
  }

  function toDataUri(svg) {
    return "data:image/svg+xml;charset=utf-8," + encodeURIComponent(svg);
  }

  function copyText(value, button) {
    if (!navigator.clipboard || !navigator.clipboard.writeText) {
      return Promise.reject(new Error("Clipboard API unavailable"));
    }

    return navigator.clipboard.writeText(value).then(function () {
      if (!button) {
        return;
      }

      var previous = button.textContent;
      button.textContent = "Copied";
      window.setTimeout(function () {
        button.textContent = previous;
      }, 1600);
    });
  }

  function createCardElement(card, svg) {
    var article = document.createElement("article");
    article.className = "card-item";

    var visual = document.createElement("div");
    visual.className = "card-visual";
    visual.innerHTML = svg;
    article.appendChild(visual);

    var meta = document.createElement("div");
    meta.className = "card-meta";

    var row = document.createElement("div");
    row.className = "meta-row";
    [card.countdown, card.slug].forEach(function (value) {
      var chip = document.createElement("span");
      chip.className = "meta-chip";
      chip.textContent = value;
      row.appendChild(chip);
    });

    if (PILOT_SLUGS.has(card.slug)) {
      var pilot = document.createElement("span");
      pilot.className = "meta-chip";
      pilot.textContent = "pilot";
      row.appendChild(pilot);
    }

    meta.appendChild(row);

    if (card.linkedin_copy) {
      var social = document.createElement("section");
      social.className = "social-copy";

      var socialHeader = document.createElement("div");
      socialHeader.className = "social-copy-header";

      var socialLabel = document.createElement("p");
      socialLabel.className = "social-copy-label";
      socialLabel.textContent = "LinkedIn copy";
      socialHeader.appendChild(socialLabel);

      var socialButton = document.createElement("button");
      socialButton.className = "copy-button";
      socialButton.type = "button";
      socialButton.textContent = "Copy text";
      socialButton.addEventListener("click", function () {
        copyText(card.linkedin_copy, socialButton).catch(function () {
          socialButton.textContent = "Copy failed";
          window.setTimeout(function () {
            socialButton.textContent = "Copy text";
          }, 1600);
        });
      });
      socialHeader.appendChild(socialButton);
      social.appendChild(socialHeader);

      var socialBody = document.createElement("textarea");
      socialBody.className = "social-copy-body";
      socialBody.readOnly = true;
      socialBody.spellcheck = false;
      socialBody.rows = Math.max(5, card.linkedin_copy.split("\n").length + 1);
      socialBody.value = card.linkedin_copy;
      social.appendChild(socialBody);

      meta.appendChild(social);
    }

    var sources = document.createElement("ul");
    sources.className = "source-list";
    card.source_ref.forEach(function (ref) {
      var item = document.createElement("li");
      item.textContent = ref;
      sources.appendChild(item);
    });
    meta.appendChild(sources);

    var download = document.createElement("a");
    download.className = "download-link";
    download.download = filenameForCard(card);
    download.href = toDataUri(svg);
    download.textContent = "Download SVG";
    meta.appendChild(download);

    article.appendChild(meta);
    return article;
  }

  async function loadJson(url) {
    var response = await fetch(url);
    if (!response.ok) {
      throw new Error("Failed to load " + url + ": " + response.status);
    }
    return response.json();
  }

  async function loadText(url) {
    var response = await fetch(url);
    if (!response.ok) {
      throw new Error("Failed to load " + url + ": " + response.status);
    }
    return response.text();
  }

  async function loadBrowserAssets() {
    var rawCards = await loadJson(ASSET_PATHS.data);
    var cards = rawCards.map(normalizeCard);
    var uniqueIcons = Array.from(new Set(cards.map(function (card) { return card.icon; })));
    var logoText = await loadText(ASSET_PATHS.logo);
    var icons = {};

    await Promise.all(uniqueIcons.map(async function (iconName) {
      icons[iconName] = parseSvgAsset(await loadText(ASSET_PATHS.iconDir + "/" + iconName + ".svg"));
    }));

    return {
      cards: cards,
      icons: icons,
      logo: parseSvgAsset(logoText),
      wireguardOfficial: parseSvgAsset(await loadText(ASSET_PATHS.iconDir + "/wireguard-official.svg"))
    };
  }

  function setStatus(message, isError) {
    var panel = document.getElementById("status-panel");
    var text = document.getElementById("status-text");
    if (!panel || !text) {
      return;
    }

    panel.classList.toggle("is-error", Boolean(isError));
    text.innerHTML = message;
  }

  function renderSection(cards, container, assets) {
    container.innerHTML = "";
    cards.forEach(function (card) {
      var svg = createCardSvg(card, assets);
      container.appendChild(createCardElement(card, svg));
    });
  }

  async function bootPreview() {
    try {
      var assets = await loadBrowserAssets();
      var pilotCards = assets.cards.filter(function (card) { return PILOT_SLUGS.has(card.slug); });
      var seriesCards = assets.cards.slice();
      renderSection(pilotCards, document.getElementById("pilot-grid"), assets);
      renderSection(seriesCards, document.getElementById("series-grid"), assets);
      setStatus("Loaded " + assets.cards.length + " cards. The same SVG generator feeds the preview and the export script.", false);
    } catch (error) {
      var detail = escapeHtml(error && error.message ? error.message : String(error));
      var hint = window.location.protocol === "file:"
        ? ' Local file fetches are likely blocked. Run <code>python3 -m http.server 8000</code> in this directory and reload.'
        : "";
      setStatus("Unable to load assets. " + detail + "." + hint, true);
    }
  }

  function loadNodeAssets() {
    var fs = require("fs");
    var path = require("path");
    var root = __dirname;
    var cards = JSON.parse(fs.readFileSync(path.join(root, "data", "zephyr-4.4.json"), "utf8")).map(normalizeCard);
    var icons = {};
    var uniqueIcons = Array.from(new Set(cards.map(function (card) { return card.icon; })));

    uniqueIcons.forEach(function (iconName) {
      icons[iconName] = parseSvgAsset(fs.readFileSync(path.join(root, "icons", iconName + ".svg"), "utf8"));
    });

    return {
      root: root,
      cards: cards,
      icons: icons,
      logo: parseSvgAsset(fs.readFileSync(path.join(root, "..", "..", "_static", "images", "logo.svg"), "utf8")),
      wireguardOfficial: parseSvgAsset(fs.readFileSync(path.join(root, "icons", "wireguard-official.svg"), "utf8"))
    };
  }

  function exportSvgCards(args) {
    var fs = require("fs");
    var path = require("path");
    var assets = loadNodeAssets();
    var selection = args.length === 0
      ? assets.cards
      : assets.cards.filter(function (card) { return args.includes(card.slug); });

    if (selection.length === 0) {
      throw new Error("No matching card slug found.");
    }

    var outDir = path.join(assets.root, "out", "svg");
    fs.mkdirSync(outDir, { recursive: true });

    selection.forEach(function (card) {
      var svg = createCardSvg(card, assets);
      fs.writeFileSync(path.join(outDir, filenameForCard(card)), svg, "utf8");
    });

    return selection.map(function (card) {
      return path.join(outDir, filenameForCard(card));
    });
  }

  function runCli() {
    var args = process.argv.slice(2);
    var command = args[0];

    if (command === "export-svg") {
      var output = exportSvgCards(args.slice(1));
      process.stdout.write(output.join("\n") + "\n");
      return;
    }

    process.stderr.write("Usage: node app.js export-svg [slug...]\n");
    process.exitCode = 1;
  }

  if (typeof window !== "undefined") {
    window.addEventListener("DOMContentLoaded", bootPreview);
  } else if (typeof module !== "undefined" && require.main === module) {
    try {
      runCli();
    } catch (error) {
      process.stderr.write((error && error.message ? error.message : String(error)) + "\n");
      process.exitCode = 1;
    }
  }
})();

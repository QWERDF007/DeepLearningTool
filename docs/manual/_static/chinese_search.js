(function () {
  "use strict";

  function splitChineseQuery(query) {
    if (!query) return [];

    const rawTokens = query
      .split(/[^\p{Letter}\p{Number}_\p{Emoji_Presentation}]+/gu)
      .filter((term) => term);

    const termsDict = new Set();
    if (typeof Search !== "undefined" && Search._index) {
      if (Search._index.terms) {
        for (const k in Search._index.terms) {
          if (Object.prototype.hasOwnProperty.call(Search._index.terms, k)) {
            termsDict.add(k.toLowerCase());
          }
        }
      }
      if (Search._index.titleterms) {
        for (const k in Search._index.titleterms) {
          if (Object.prototype.hasOwnProperty.call(Search._index.titleterms, k)) {
            termsDict.add(k.toLowerCase());
          }
        }
      }
    }

    const result = [];
    for (const token of rawTokens) {
      if (!/[\u4e00-\u9fa5]/.test(token) || termsDict.has(token.toLowerCase())) {
        result.push(token);
        continue;
      }

      let i = 0;
      const subTokens = [];
      while (i < token.length) {
        if (/^[A-Za-z0-9_]/.test(token[i])) {
          const alphaMatch = token.slice(i).match(/^[A-Za-z0-9_]+/);
          const alphaWord = alphaMatch ? alphaMatch[0] : token[i];
          let matchedAlpha = null;
          for (let len = alphaWord.length; len >= 2; len--) {
            const sub = alphaWord.substr(0, len);
            if (termsDict.has(sub.toLowerCase())) {
              matchedAlpha = sub;
              break;
            }
          }
          if (matchedAlpha) {
            subTokens.push(matchedAlpha);
            i += matchedAlpha.length;
          } else {
            subTokens.push(alphaWord);
            i += alphaWord.length;
          }
          continue;
        }

        let matched = null;
        for (let len = Math.min(10, token.length - i); len >= 2; len--) {
          const sub = token.substr(i, len);
          if (termsDict.has(sub.toLowerCase())) {
            matched = sub;
            break;
          }
        }
        if (matched) {
          subTokens.push(matched);
          i += matched.length;
        } else {
          subTokens.push(token[i]);
          i++;
        }
      }
      result.push(...subTokens.filter((t) => t.trim()));
    }

    return result;
  }

  window.splitQuery = splitChineseQuery;
})();

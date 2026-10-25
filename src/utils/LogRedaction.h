#pragma once

#include <QRegularExpression>
#include <QString>

// Redact supported Jellyfin credential forms, without decoding or logging input.
// Fixed marker expressions and a forward value scan preserve message size and
// avoid unbounded regex backtracking on long or malformed log messages.
namespace LogRedaction {
inline bool encodedDelimiter(const QString& text, qsizetype at)
{
  if (at + 2 >= text.size() || text[at] != QLatin1Char('%')) return false;
  const auto code = text.mid(at + 1, 2).toLower();
  return code == QStringLiteral("22") || code == QStringLiteral("27") ||
    code == QStringLiteral("26") || code == QStringLiteral("23") ||
    code == QStringLiteral("2c") || code == QStringLiteral("20") ||
    code == QStringLiteral("0a") || code == QStringLiteral("0d");
}

inline void value(QString& text, qsizetype start)
{
  auto end = start;
  while (end < text.size()) {
    const auto c = text[end];
    if (c.isSpace() || c == QLatin1Char('&') || c == QLatin1Char('#') ||
        c == QLatin1Char('"') || c == QLatin1Char('\'') || c == QLatin1Char(',') ||
        c == QLatin1Char('}') || c == QLatin1Char(']') || c == QLatin1Char('\\') ||
        c == QLatin1Char('<') || c == QLatin1Char('>') || encodedDelimiter(text, end)) break;
    ++end;
  }
  for (auto index = start; index < end; ++index) text[index] = QLatin1Char('x');
}

inline void censor(QString& text)
{
  const auto insensitive = QRegularExpression::CaseInsensitiveOption;
  // Optional literal/JSON-escaped/URL-encoded quotes around keys and values.
  static const QRegularExpression credentials(QStringLiteral(
    R"((?:(?<![A-Za-z0-9_])|(?<=%22)|(?<=%27)|(?<=%26)|(?<=%3f)|(?<=%2c)|(?<=%20)|(?<=%0a)|(?<=%0d))(?:api_key|apikey|accesstoken|x-mediabrowser-token)(?:\\?["']|%22|%27)?\s*(?:=|:|%3d|%3a)\s*(?:\\?["']|%22|%27)?)"), insensitive);
  static const QRegularExpression bearer(QStringLiteral(
    R"((?:(?<![A-Za-z0-9_])|(?<=%22)|(?<=%27)|(?<=%26)|(?<=%3f)|(?<=%2c)|(?<=%20)|(?<=%0a)|(?<=%0d))authorization(?:\\?["']|%22|%27)?\s*(?:=|:|%3d|%3a)\s*(?:\\?["']|%22|%27)?\s*bearer(?:\s|%20|\+)+)"), insensitive);
  for (const auto* pattern : {&credentials, &bearer}) {
    auto matches = pattern->globalMatch(text);
    while (matches.hasNext()) value(text, matches.next().capturedEnd());
  }
  // A bare Token field is a credential only in a MediaBrowser/Emby header.
  // Match scheme markers separately instead of scanning the whole line once
  // per Token, so repeated malformed markers do not cause quadratic work.
  static const QRegularExpression headers(QStringLiteral(
    R"((?:(?<![A-Za-z0-9_])|(?<=%22)|(?<=%27)|(?<=%26)|(?<=%3f)|(?<=%2c)|(?<=%20)|(?<=%0a)|(?<=%0d))(?:mediabrowser|emby)\b|(?:(?<![A-Za-z0-9_])|(?<=%22)|(?<=%27)|(?<=%26)|(?<=%3f)|(?<=%2c)|(?<=%20)|(?<=%0a)|(?<=%0d))(token(?:\\?["']|%22|%27)?\s*(?:=|:|%3d|%3a)\s*(?:\\?["']|%22|%27)?))"), insensitive);
  qsizetype headerEnd = -1;
  auto matches = headers.globalMatch(text);
  while (matches.hasNext()) {
    const auto match = matches.next();
    if (match.capturedStart(1) < 0 && match.capturedStart() >= headerEnd) {
      headerEnd = text.indexOf(QLatin1Char('\n'), match.capturedEnd());
      if (headerEnd < 0) headerEnd = text.size();
    } else if (match.capturedStart(1) >= 0 && match.capturedStart() < headerEnd) {
      value(text, match.capturedEnd());
    }
  }
}
}

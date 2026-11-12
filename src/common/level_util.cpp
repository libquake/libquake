#include "level_util.h"
#include <algorithm>
#include <cctype>

#include <cmath>
#include <stdexcept>

namespace quakelib::detail {
  namespace {
    class Tokenizer {
    public:
      explicit Tokenizer(const std::string &text) : m_text(text) {}

      // next token: "{", "}" or the contents of a quoted string, false at the end
      bool Next(std::string &token, bool &quoted) {
        skipSpaceAndComments();
        if (m_pos >= m_text.size())
          return false;
        char c = m_text[m_pos];
        if (c == '{' || c == '}') {
          token.assign(1, c);
          quoted = false;
          m_pos++;
          return true;
        }
        if (c == '"') {
          size_t end = m_text.find('"', m_pos + 1);
          if (end == std::string::npos)
            throw std::runtime_error("unterminated string in entity data");
          token = m_text.substr(m_pos + 1, end - m_pos - 1);
          quoted = true;
          m_pos = end + 1;
          return true;
        }
        size_t end = m_pos;
        while (end < m_text.size() && !std::isspace((unsigned char)m_text[end]) && m_text[end] != '"' &&
               m_text[end] != '{' && m_text[end] != '}')
          end++;
        token = m_text.substr(m_pos, end - m_pos);
        quoted = false;
        m_pos = end;
        return true;
      }

    private:
      void skipSpaceAndComments() {
        while (m_pos < m_text.size()) {
          char c = m_text[m_pos];
          if (std::isspace((unsigned char)c) || c == '\0') {
            m_pos++;
          } else if (c == '/' && m_pos + 1 < m_text.size() && m_text[m_pos + 1] == '/') {
            size_t nl = m_text.find('\n', m_pos);
            m_pos = nl == std::string::npos ? m_text.size() : nl + 1;
          } else {
            break;
          }
        }
      }

      const std::string &m_text;
      size_t m_pos = 0;
    };
  } // namespace

  std::vector<LevelEntity> ParseEntityText(const std::string &text) {
    std::vector<LevelEntity> entities;
    Tokenizer tok(text);
    std::string token, value;
    bool quoted = false;
    while (tok.Next(token, quoted)) {
      if (quoted || token != "{")
        throw std::runtime_error("expected '{' in entity data, got '" + token + "'");
      LevelEntity ent;
      for (;;) {
        if (!tok.Next(token, quoted))
          throw std::runtime_error("unexpected end of entity data");
        if (!quoted && token == "}")
          break;
        if (!tok.Next(value, quoted) || !quoted)
          throw std::runtime_error("missing value for key '" + token + "'");
        // later keys win, like in the engine
        ent.attributes[token] = value;
      }
      ent.classname = ent.Get("classname");
      entities.push_back(std::move(ent));
    }
    return entities;
  }

  std::vector<std::string> SplitWadKey(const std::string &value) {
    std::vector<std::string> wads;
    size_t start = 0;
    while (start <= value.size()) {
      size_t end = value.find(';', start);
      if (end == std::string::npos)
        end = value.size();
      std::string item = value.substr(start, end - start);
      size_t slash = item.find_last_of("/\\");
      if (slash != std::string::npos)
        item = item.substr(slash + 1);
      while (!item.empty() && std::isspace((unsigned char)item.front()))
        item.erase(item.begin());
      while (!item.empty() && std::isspace((unsigned char)item.back()))
        item.pop_back();
      if (!item.empty() && std::find(wads.begin(), wads.end(), item) == wads.end())
        wads.push_back(item);
      start = end + 1;
    }
    return wads;
  }

  Vec4 TangentFromGradients(const Vec3 &normal, const Vec3 &gradU, const Vec3 &gradV) {
    auto cross = [](const double *a, const double *b, double *o) {
      o[0] = a[1] * b[2] - a[2] * b[1];
      o[1] = a[2] * b[0] - a[0] * b[2];
      o[2] = a[0] * b[1] - a[1] * b[0];
    };
    auto dot = [](const double *a, const double *b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; };
    double n[3] = {normal[0], normal[1], normal[2]};
    double gu[3] = {gradU[0], gradU[1], gradU[2]};
    double gv[3] = {gradV[0], gradV[1], gradV[2]};

    // the in-plane directions dP/du and dP/dv, these differ from the gradients whenever the projection is
    // skewed
    double dPdu[3], dPdv[3];
    cross(gv, n, dPdu);
    cross(n, gu, dPdv);
    double du = dot(gu, dPdu);
    double dv = dot(gv, dPdv);
    double len = std::sqrt(dot(dPdu, dPdu));
    if (std::abs(du) < 1e-12 || std::abs(dv) < 1e-12 || len < 1e-12)
      return Vec4{0, 0, 0, 1};

    double su = du > 0 ? 1.0 : -1.0;
    double t[3] = {dPdu[0] * su / len, dPdu[1] * su / len, dPdu[2] * su / len};
    double b[3] = {dPdv[0] * (dv > 0 ? 1.0 : -1.0), dPdv[1] * (dv > 0 ? 1.0 : -1.0),
                   dPdv[2] * (dv > 0 ? 1.0 : -1.0)};
    double nt[3];
    cross(n, t, nt);
    double w = dot(nt, b) >= 0 ? 1.0 : -1.0;
    return Vec4{(float)t[0], (float)t[1], (float)t[2], (float)w};
  }

} // namespace quakelib::detail

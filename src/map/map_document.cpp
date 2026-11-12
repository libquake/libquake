#include "../common/level_util.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <quakelib/map/map_document.h>
#include <sstream>
#include <stdexcept>

namespace quakelib {
  namespace {
    enum class Tok {
      End,
      OpenBrace,
      CloseBrace,
      OpenParen,
      CloseParen,
      OpenBracket,
      CloseBracket,
      String,
      Word
    };

    class MapLexer {
    public:
      explicit MapLexer(const std::string &text) : m_text(text) {}

      Tok Next() {
        skipSpaceAndComments();
        m_tokenLine = m_line;
        if (m_pos >= m_text.size())
          return m_tok = Tok::End;

        char c = m_text[m_pos];
        switch (c) {
        case '(':
          m_pos++;
          return m_tok = Tok::OpenParen;
        case ')':
          m_pos++;
          return m_tok = Tok::CloseParen;
        case '[':
          m_pos++;
          return m_tok = Tok::OpenBracket;
        case ']':
          m_pos++;
          return m_tok = Tok::CloseBracket;
        case '"': {
          size_t end = m_text.find('"', m_pos + 1);
          if (end == std::string::npos)
            Fail("unterminated string");
          m_value = m_text.substr(m_pos + 1, end - m_pos - 1);
          m_line += (int)std::count(m_value.begin(), m_value.end(), '\n');
          m_pos = end + 1;
          return m_tok = Tok::String;
        }
        default:
          break;
        }

        // texture names may start with '{' (transparent textures), braces are only tokens on their own
        size_t end = m_pos;
        while (end < m_text.size() && !std::isspace((unsigned char)m_text[end]) && m_text[end] != '(' &&
               m_text[end] != ')' && m_text[end] != '[' && m_text[end] != ']' && m_text[end] != '"')
          end++;
        m_value = m_text.substr(m_pos, end - m_pos);
        m_pos = end;
        if (m_value == "{")
          return m_tok = Tok::OpenBrace;
        if (m_value == "}")
          return m_tok = Tok::CloseBrace;
        return m_tok = Tok::Word;
      }

      Tok Peek() {
        size_t pos = m_pos;
        int line = m_line;
        std::string value = m_value;
        Tok cur = m_tok;
        int tokenLine = m_tokenLine;
        Tok t = Next();
        m_pos = pos;
        m_line = line;
        m_value = value;
        m_tok = cur;
        m_tokenLine = tokenLine;
        return t;
      }

      void Expect(Tok t, const char *what) {
        if (Next() != t)
          Fail(std::string("expected ") + what);
      }

      double Number() {
        if (Next() != Tok::Word)
          Fail("expected a number");
        char *end = nullptr;
        double d = std::strtod(m_value.c_str(), &end);
        if (end == m_value.c_str() || *end != '\0')
          Fail("expected a number, got '" + m_value + "'");
        return d;
      }

      [[noreturn]] void Fail(const std::string &msg) const {
        throw std::runtime_error("line " + std::to_string(m_tokenLine) + ": " + msg);
      }

      const std::string &Value() const { return m_value; }

      int Line() const { return m_tokenLine; }

    private:
      void skipSpaceAndComments() {
        while (m_pos < m_text.size()) {
          char c = m_text[m_pos];
          if (c == '\n') {
            m_line++;
            m_pos++;
          } else if (std::isspace((unsigned char)c) || c == '\0') {
            m_pos++;
          } else if (c == '/' && m_pos + 1 < m_text.size() && m_text[m_pos + 1] == '/') {
            size_t nl = m_text.find('\n', m_pos);
            m_pos = nl == std::string::npos ? m_text.size() : nl;
          } else {
            break;
          }
        }
      }

      const std::string &m_text;
      size_t m_pos = 0;
      int m_line = 1;
      int m_tokenLine = 1;
      Tok m_tok = Tok::End;
      std::string m_value;
    };

    MapFace parseFace(MapLexer &lex) {
      MapFace face;
      // the opening parenthesis of the first point is already consumed
      for (int p = 0; p < 3; p++) {
        if (p > 0)
          lex.Expect(Tok::OpenParen, "'(' of a plane point");
        for (int k = 0; k < 3; k++)
          face.points[p][k] = lex.Number();
        lex.Expect(Tok::CloseParen, "')' of a plane point");
      }

      Tok t = lex.Next();
      if (t != Tok::Word && t != Tok::String)
        lex.Fail("expected a texture name");
      face.texture = lex.Value();

      if (lex.Peek() == Tok::OpenBracket) {
        face.valve = true;
        for (auto *axis : {&face.uAxis, &face.vAxis}) {
          lex.Expect(Tok::OpenBracket, "'['");
          for (int k = 0; k < 4; k++)
            (*axis)[k] = (float)lex.Number();
          lex.Expect(Tok::CloseBracket, "']'");
        }
        face.offsetU = face.uAxis[3];
        face.offsetV = face.vAxis[3];
      } else {
        face.offsetU = (float)lex.Number();
        face.offsetV = (float)lex.Number();
      }
      face.rotation = (float)lex.Number();
      face.scaleX = (float)lex.Number();
      face.scaleY = (float)lex.Number();

      // quake 2 style content / surface flags and value, not used
      while (lex.Peek() == Tok::Word)
        lex.Next();
      return face;
    }

    MapBrush parseBrush(MapLexer &lex) {
      MapBrush brush;
      brush.line = lex.Line();
      for (;;) {
        Tok t = lex.Next();
        if (t == Tok::CloseBrace)
          break;
        if (t == Tok::OpenParen) {
          brush.faces.push_back(parseFace(lex));
          continue;
        }
        if (t == Tok::Word && (lex.Value() == "brushDef" || lex.Value() == "brushDef3" ||
                               lex.Value() == "patchDef2" || lex.Value() == "patchDef3"))
          lex.Fail("Quake 3 / Doom 3 brush formats are not supported");
        lex.Fail("expected a brush face or '}'");
      }
      return brush;
    }

    void addTexture(MapDocument &doc, const std::string &name) {
      if (std::find(doc.textures.begin(), doc.textures.end(), name) == doc.textures.end())
        doc.textures.push_back(name);
    }
  } // namespace

  MapDocument ParseMapText(const std::string &text) {
    MapDocument doc;
    MapLexer lex(text);
    for (Tok t = lex.Next(); t != Tok::End; t = lex.Next()) {
      if (t != Tok::OpenBrace)
        lex.Fail("expected '{' to start an entity");
      MapEntity ent;
      ent.line = lex.Line();
      for (;;) {
        t = lex.Next();
        if (t == Tok::CloseBrace)
          break;
        if (t == Tok::String) {
          std::string key = lex.Value();
          if (lex.Next() != Tok::String)
            lex.Fail("missing value for key '" + key + "'");
          // later keys win, like in the engine
          ent.properties.attributes[key] = lex.Value();
          continue;
        }
        if (t == Tok::OpenBrace) {
          ent.brushes.push_back(parseBrush(lex));
          continue;
        }
        if (t == Tok::End)
          lex.Fail("unexpected end of file inside an entity");
        lex.Fail("expected a key, a brush or '}'");
      }
      ent.properties.classname = ent.properties.Get("classname");
      for (const auto &b : ent.brushes)
        for (const auto &f : b.faces)
          addTexture(doc, f.texture);
      doc.entities.push_back(std::move(ent));
    }

    for (const auto &e : doc.entities) {
      if (e.properties.classname == "worldspawn") {
        doc.wads = detail::SplitWadKey(e.properties.Get("wad"));
        break;
      }
    }
    return doc;
  }

  MapDocument ParseMap(const std::string &path) {
    std::ifstream file(path, std::ios::binary);
    if (!file)
      throw std::runtime_error("cannot open " + path);
    std::stringstream buffer;
    buffer << file.rdbuf();
    try {
      return ParseMapText(buffer.str());
    } catch (const std::runtime_error &e) {
      throw std::runtime_error(path + ": " + e.what());
    }
  }

} // namespace quakelib

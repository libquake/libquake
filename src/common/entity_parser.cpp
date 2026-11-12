#include <quakelib/entity_parser.h>

#include <iostream>
#include <regex>
#include <string>

namespace quakelib {

  using std::string;

  static std::vector<std::string> rexec_vec(std::string line, const std::string &regexstr) {
    std::stringstream results;

    std::regex re(regexstr, std::regex::icase);

    std::vector<std::string> matches;
    const std::sregex_token_iterator end;
    for (std::sregex_token_iterator it(line.begin(), line.end(), re, 1); it != end; it++) {
      matches.push_back(*it);
    }

    return matches;
  }

  inline std::string &ltrim(std::string &s, const char *t = " ") {
    s.erase(0, s.find_first_not_of(t));
    return s;
  }

  void EntityParser::ParseEntites(std::istream &strstr, EntityParsedFunc fn) {
    std::vector<ParsedEntity *> objects;
    ParsedEntity *current = nullptr;
    bool foundWorldSpawn = false;

    for (std::string line; strstr.peek() != EOF && std::getline(strstr, line);) {
      std::erase(line, '\r');
      std::replace(line.begin(), line.end(), '\t', ' ');

      if (line.empty()) {
        continue;
      }
      line = ltrim(line);

      if (line == "// Format: Valve") {
      }

      if (line.starts_with("//")) {
        continue;
      }
      if (line == "{") {
        ParsedEntity *newobj = new ParsedEntity;
        if (current == nullptr) {
          objects.push_back(newobj);
          current = newobj;
        } else {
          newobj->parent = current;
          current->children.push_back(newobj);
          if (current->type != EntityType::WORLDSPAWN) {
            current->type = EntityType::SOLID;
          }
          current = newobj;
        }
        continue;
      }
      if (line == "}") {
        if (current != nullptr) {
          current = current->parent;
        }
        continue;
      }
      if (current != nullptr) {
        current->lines << line << std::endl;

        if (current->parent == nullptr && line.rfind("\"model\" \"*", 0) != std::string::npos) {
          current->type = EntityType::SOLID;
        }

        if (!foundWorldSpawn && line == ("\"classname\" \"worldspawn\"")) {
          current->type = EntityType::WORLDSPAWN;
          foundWorldSpawn = true;
        }
      }
    }

    for (const auto &obj : objects) {
      fn(obj);
    }

    return;
  }

  void EntityParser::ParseEntites(const std::string &buffer, EntityParsedFunc fn) {
    auto strstr = std::stringstream(buffer);
    ParseEntites(strstr, fn);
  }

  void Entity::FillFromParsed(ParsedEntity *pe) {
    attribMap_t all;
    for (std::string l; std::getline(pe->lines, l);) {
      auto matches = rexec_vec(l, R"(\"([\s\S]*?)\")");
      if (matches.size() < 2) {
        continue;
      }
      all.emplace(matches[0], matches[1]);
    }
    SetFromAttributes(all);
  }

  void Entity::SetFromAttributes(const attribMap_t &all) {
    for (const auto &[key, value] : all) {
      if (key == "classname") {
        m_classname = value;
      } else if (key == "_tb_name") {
        m_tbName = value;
      } else if (key == "_tb_type") {
        m_tbType = value;
      } else {
        m_attributes.emplace(key, value);
      }
    }
  }

  void PointEntity::SetFromAttributes(const attribMap_t &all) {
    Entity::SetFromAttributes(all);
    m_origin = AttributeVec3("origin");
    m_attributes.erase("origin");
    m_angle = AttributeFloat("angle");
    m_attributes.erase("angle");
  }

  void SolidEntity::SetFromAttributes(const attribMap_t &all) {
    Entity::SetFromAttributes(all);
    m_hasPhongShading = AttributeFloat("_phong");
    m_attributes.erase("_phong");
  }

  void WorldSpawnEntity::SetFromAttributes(const attribMap_t &all) {
    SolidEntity::SetFromAttributes(all);
    auto wadstr = AttributeStr("wad");
    std::istringstream ss(wadstr);

    for (std::string item; std::getline(ss, item, ';');) {
      m_wads.push_back(item.substr(item.find_last_of("/\\") + 1));
    }

    m_attributes.erase("wad");
  }

} // namespace quakelib
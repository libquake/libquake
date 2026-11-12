#pragma once

#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include <quakelib/qmath.h>
#include <quakelib/surface.h>

namespace quakelib {

  /**
   * @brief Defines the basic types of entities available in Quake maps.
   */
  enum class EntityType {
    POINT = 0,      ///< A point entity with no geometry (e.g., lights, spawns).
    SOLID = 1,      ///< A brush-based entity with geometry (e.g., triggers, doors).
    WORLDSPAWN = 2, ///< The worldspawn entity containing global map data.
  };

  /**
   * @brief Map type for storing key-value attribute pairs.
   */
  using attribMap_t = std::map<std::string, std::string>;

  /**
   * @brief Intermediate structure representing a raw parsed entity.
   *
   * This structure holds the raw string data and hierarchy before it is
   * processed into a concrete Entity subclass.
   */
  struct ParsedEntity {
    std::stringstream lines;              ///< Raw text lines belonging to this entity.
    ParsedEntity *parent = nullptr;       ///< Pointer to the parent parsed entity, if any.
    std::vector<ParsedEntity *> children; ///< List of child parsed entities.
    EntityType type = EntityType::POINT;  ///< The inferred type of the entity.
  };

  /**
   * @brief Base class for all Quake entities.
   *
   * Provides common functionality for attribute management and type identification.
   */
  class Entity {
  public:
    /**
     * @brief Constructs an entity with a specific type.
     * @param type The type of the entity.
     */
    explicit Entity(EntityType type) : m_type(type) {};

    /**
     * @brief Populates the entity data from a ParsedEntity structure.
     * @param pe Pointer to the parsed entity data.
     */
    void FillFromParsed(ParsedEntity *pe);

    /**
     * @brief Sets classname and attributes from all key / value pairs of the entity.
     */
    virtual void SetFromAttributes(const attribMap_t &all);

    /**
     * @brief Gets the classname of the entity.
     * @return The "classname" attribute value.
     */
    [[nodiscard]] const std::string &ClassName() const;

    /**
     * @brief Checks if the classname contains a specific substring.
     * @param substr The substring to search for.
     * @return True if the classname contains the substring, false otherwise.
     */
    [[nodiscard]] bool ClassContains(const std::string &substr) const;

    // Attribute Methods

    /**
     * @brief Gets all attributes associated with this entity.
     * @return A map of attribute keys and values.
     */
    const attribMap_t &Attributes() const;

    /**
     * @brief Retrieves a string attribute value.
     * @param classname The attribute key (often matches "classname" but used generically here).
     * @return The attribute value as a string.
     */
    std::string AttributeStr(const std::string &classname);

    /**
     * @brief Retrieves a floating-point attribute value.
     * @param classname The attribute key.
     * @return The attribute value as a float.
     */
    float AttributeFloat(const std::string &classname);

    /**
     * @brief Retrieves a 3D vector attribute value.
     * @param classname The attribute key.
     * @return The attribute value as a vec3.
     */
    math::Vec3 AttributeVec3(const std::string &classname);

    /**
     * @brief Retrieves a 2D vector attribute value.
     * @param classname The attribute key.
     * @return The attribute value as a vec2.
     */
    math::Vec2 AttributeVec2(const std::string &classname);

  protected:
    attribMap_t m_attributes;              ///< Map of all key-value attributes.
    std::string m_classname;               ///< Cached classname of the entity.
    std::string m_tbName;                  ///< TrenchBroom specific name.
    std::string m_tbType;                  ///< TrenchBroom specific type.
    EntityType m_type = EntityType::POINT; ///< The type of this entity.
  };

  /**
   * @brief Represents a point entity.
   *
   * Point entities are defined by an origin and do not have associated brush geometry.
   */
  class PointEntity : public Entity {
  public:
    /**
     * @brief Default constructor for PointEntity.
     */
    PointEntity() : Entity(EntityType::POINT) {};

    void SetFromAttributes(const attribMap_t &all) override;

    /**
     * @brief Gets the origin of the point entity.
     * @return The position vector.
     */
    const math::Vec3 &Origin() const;

    /**
     * @brief Sets the origin of the point entity.
     * @param origin The new position vector.
     */
    void SetOrigin(const math::Vec3 &origin) { m_origin = origin; }

    /**
     * @brief Gets the angle of the point entity.
     * @return The angle in degrees.
     */
    float Angle();

  private:
    math::Vec3 m_origin{0}; ///< The 3D position of the entity.
    float m_angle = 0;      ///< The orientation angle.
  };

  /**
   * @brief Represents a solid (brush-based) entity.
   *
   * Solid entities contain one or more brushes (geometry) defined by surfaces.
   */
  class SolidEntity : public Entity {
  public:
    /**
     * @brief Default constructor for SolidEntity.
     */
    SolidEntity() : Entity(EntityType::SOLID) {};

    void SetFromAttributes(const attribMap_t &all) override;

  protected:
    std::vector<Surface> m_surfaces; ///< Collection of surfaces forming the brushes.
    bool m_hasPhongShading = false;  ///< Flag indicating if Phong shading is enabled.
  };

  /**
   * @brief Represents the WorldSpawn entity.
   *
   * This particular entity represents the world geometry and global settings.
   */
  class WorldSpawnEntity : public SolidEntity {
  public:
    void SetFromAttributes(const attribMap_t &all) override;

  private:
    std::vector<std::string> m_wads; ///< List of WAD files used by the map.
  };

  using EntityPtr = std::shared_ptr<Entity>;
  using SolidEntityPtr = std::shared_ptr<SolidEntity>;
  using PointEntityPtr = std::shared_ptr<PointEntity>;

} // namespace quakelib
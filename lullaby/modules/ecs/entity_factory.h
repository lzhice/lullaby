/*
Copyright 2017-2019 Google Inc. All Rights Reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS-IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
*/

#ifndef LULLABY_MODULES_ECS_ENTITY_FACTORY_H_
#define LULLABY_MODULES_ECS_ENTITY_FACTORY_H_

#include <cstddef>
#include <list>
#include <mutex>
#include <queue>
#include <string>
#include <unordered_map>
#include <utility>

#include "flatbuffers/flatbuffers.h"
#include "lullaby/modules/ecs/blueprint.h"
#include "lullaby/modules/ecs/blueprint_tree.h"
#include "lullaby/modules/ecs/component_handlers.h"
#include "lullaby/modules/file/asset.h"
#include "lullaby/util/dependency_checker.h"
#include "lullaby/util/entity.h"
#include "lullaby/util/optional.h"
#include "lullaby/util/registry.h"
#include "lullaby/util/resource_manager.h"
#include "lullaby/util/string_view.h"
#include "lullaby/util/typeid.h"

namespace lull {

class System;

// Creates Entities and associated Components from Blueprints.
//
// All Systems must be created/added to the EntityFactory in order to correctly
// create Entities.  The EntityFactory will then be responsible for calling
// Initialize() on all Systems which allows Systems to perform any operations
// that may be dependent on other Systems.
//
// In addition to creating Entities from Blueprints, the EntityFactory can be
// used to create Entities (and associated Components) from assets stored on
// disk.  These assets are flatbuffer binary representations of BlueprintDefs
// from the build_blueprint_bin BUILD rule.
//
// The flatbuffer schemas for Components should be compiled with
// generate_schema_cc_library, which produces the autogenerated "ComponentDefT"
// classes used as convenient runtime wrappers for corresponding flatbuffer
// "ComponentDef" types. The EntityFactory converts between raw binary asset
// data and Blueprint instances, and then ComponentDefT classes can then be read
// from the Blueprint. Also, ComponentDefT classes can be written to a
// Blueprint, then Finalized before being saved to disk or transmitted over the
// wire.
//
// Other than a few key functions, this class is not thread-safe.
//
// IMPORTANT: There are several ways to Initialize the EntityFactory.  In all
// cases, the Initialize() function MUST be called after all Systems have been
// created and/or added to the EntityFactory.
//
// DEPRECATED: The legacy method of using EntityFactory involves defining
// client-specific EntityDef nad ComponentDef classes using
// generate_entity_schema, then compiling Entities with build_entity_bin. This
// is now deprecated.
class EntityFactory {
 public:
  // Dictionary of Entity to the name of the blueprint that created that entity.
  using BlueprintMap = std::unordered_map<Entity, std::string>;

  explicit EntityFactory(Registry* registry);
  ~EntityFactory();

  // Create and register a new EntityFactory in the Registry.
  static EntityFactory* Create(Registry* registry);

  // Creates a System of type |T| using the Registry and caches the instance
  // internally for use during Entity creation.
  template <typename T, typename... Args>
  T* CreateSystem(Args&&... args);

  // Adds the System of type |T| which exists in Registry already into
  // EntityFactory for use during Entity creation. This is an alternative to
  // CreateSystem that can be used when the System is already in Registry. This
  // is normally used by test when we put a mock system in registry ahead of
  // time.
  template <typename T>
  T* AddSystemFromRegistry();

  // Initializes all registered Systems and checks to ensure all dependencies
  // have been created.
  void Initialize();

  // Registers a System with a specific ComponentDefT type, the
  // Lullaby-specific autogenerated wrapper type for a corresponding flatbuffer
  // ComponentDef type.
  template <typename DefT>
  void RegisterDef(TypeId system_type);

  // Creates a new "empty" Entity without any Components.  This function is
  // thread-safe.
  Entity Create();

  // Creates a new Entity and associates Components with it based on the data in
  // the EntityDef Blueprint specified by |name|.  The EntityFactory loads the
  // Blueprint by appending ".bin" to the given name.  Returns kNullEntity if
  // unsuccessful.
  Entity Create(const std::string& name);

  // Creates a new Entity and associates Components with it that are contained
  // in the specified |blueprint|.  Returns kNullEntity if unsuccessful.
  Entity Create(Blueprint* blueprint);

  // Creates a new Entity hierarchy and associates Components with the entities
  // that are defined int he specified |hierarchical_blueprint|.  Returns the
  // root entity of the hierarchy, or kNullEntity if unsuccessful.
  Entity Create(BlueprintTree* blueprint);

  // Populates the specified |entity| with the data in the EntityDef Blueprint
  // specified by |name|.  Ideally, the Entity does not have any Components
  // associated with it (ie. it is a freshly created Entity from calling
  // EntityFactory::Create()). Returns the same Entity if the operation was
  // successful, kNullEntity otherwise.
  Entity Create(Entity entity, const std::string& name);
  // Similar to the above, but uses a BlueprintTree instead of the filename
  // of a blueprint.
  Entity Create(Entity entity, BlueprintTree* blueprint);

  // Creates a new Entity from raw blueprint data.  This data should not be
  // confused with the Blueprint class.  Instead, it is raw binary data from
  // operations such as loading off disk.  Returns kNullEntity if unsuccessful.
  Entity CreateFromBlueprint(const void* blueprint, size_t len,
                             const std::string& name);
  // Similar to the above, but uses a specified Entity id.  Returns false if
  // unsuccessful.
  bool CreateFromBlueprint(Entity entity, const void* blueprint, size_t len,
                           const std::string& name);

  // Serialize the |blueprint_tree| into a BlueprintDef flatbuffer binary.
  flatbuffers::DetachedBuffer Finalize(BlueprintTree* blueprint_tree);

  // Removes all components from the specified Entity effectively destroying it.
  void Destroy(Entity entity);

  // Marks an Entity for destruction.  The queue of Entities will be destroyed
  // when DestroyQueuedEntities is called.  This function is thread-safe.
  void QueueForDestruction(Entity entity);

  // Destroys the Entities marked for destruction by QueueForDestruction.
  void DestroyQueuedEntities();

  // Returns a map of entities to the name of the blueprint from which they were
  // created.
  const BlueprintMap& GetEntityToBlueprintMap() const;

  // Gets or loads off disk a blueprint asset with the given |name|.
  std::shared_ptr<SimpleAsset> GetBlueprintAsset(const std::string& name);

  // Sets the function used to make one entity a child of another.  Typically
  // set by the Transform system when it initializes.
  using CreateChildFn =
      std::function<Entity(Entity parent, BlueprintTree* bpt)>;
  void SetCreateChildFn(CreateChildFn fn) { create_child_fn_ = std::move(fn); }

  // Create a blueprint without creating an entity.
  Optional<BlueprintTree> CreateBlueprint(const std::string& name);

  // Stop caching the named blueprint.
  void ForgetCachedBlueprint(const std::string& name);

  // //////////////////////////////////////////////////////////////////////////
  // //////////////////////// DEPRECATED METHODS BELOW ////////////////////////
  // //////////////////////////////////////////////////////////////////////////

  // DEPRECATED: The legacy 4 letter file identifier used for flatbuffer's
  // file_identifier property.  The value is set to "ENTS", and corresponds to
  // the default value in generate_entity_schema.
  static const char* const kLegacyFileIdentifier;

  // DEPRECATED: Performs a full initialization of the EntityFactory using the
  // client- provided EntityDef and ComponentDef classes.  The general usage of
  // this function is:
  //     entity_factory->Initialize<EntityDef, ComponentDef>(
  //         GetEntityDef, EnumNamesComponentDefType());
  //
  // This function will perform the "basic" initialization of the EntityFactory
  // as well as initialize the Loader and Finalizer functors assuming the
  // EntityDef and ComponentDef were setup using generate_entity_schema
  // BUILD rule.
  template <typename EntityDef, typename ComponentDef, typename GetEntityDefFn>
  void Initialize(GetEntityDefFn get_entity_def,
                  const char* const* component_def_names);

  // DEPRECATED: Registers an extra Entity Schema.  This allows an app to load
  // entity blueprints from multiple entity.fbs files.  To use this, add a
  // namespace to the 2nd entity.fbs file, add a 4 letter all caps identifier
  // (i.e. "FOOB") to the generate_entity_schema rule, and call this in the
  // form:
  //
  //     entity_factory->RegisterFlatbufferConverter<namespace ::EntityDef,
  //                                                 namespace ::ComponentDef>(
  //         namespace ::GetEntityDef, namespace ::EnumNamesComponentDefType(),
  //         "FOOB");
  //
  // The default identifier is "ENTS", captured in kLegacyFileIdentifier.
  template <typename EntityDef, typename ComponentDef, typename GetEntityDefFn>
  void RegisterFlatbufferConverter(GetEntityDefFn get_entity_def,
                                   const char* const* component_def_names,
                                   string_view file_identifier);

  // DEPRECATED: Returns the number of flatbuffer converters that have been
  // registered.
  size_t GetFlatbufferConverterCount();

  // DEPRECATED: In some situations (namely tests), we need to do more than the
  // "basic" initialization, but we need to explicitly control/test the Loader
  // and Finalizers, so these functions can be used to selectively perform that
  // type of initialization.  The Initialize function takes the list of
  // ComponentDef names which are then used by the Loader and Finalizer.  Then,
  // either (or both) the Loader or Finalizer can be Initialized using
  // client-specified EntityDef and ComponentDef classes. If file_identifier is
  // not passed, it is assumed to be the default legacy value of "ENTS".
  void Initialize(const char* const* component_def_names);
  template <typename EntityDef, typename ComponentDef, typename GetEntityDefFn>
  void InitializeLoader(GetEntityDefFn get_entity_def);
  template <typename EntityDef, typename ComponentDef>
  void InitializeFinalizer();
  void Initialize(const char* const* component_def_names,
                  string_view file_identifier);
  template <typename EntityDef, typename ComponentDef, typename GetEntityDefFn>
  void InitializeLoader(GetEntityDefFn get_entity_def,
                        string_view file_identifier);
  template <typename EntityDef, typename ComponentDef>
  void InitializeFinalizer(string_view file_identifier);

  // DEPRECATED: Registers a System with a specific a ComponentDef type.  The
  // |def_type| is simply a hash of the ComponentDef's type name.
  void RegisterDef(TypeId system_type, Blueprint::DefType def_type);

  // Finalizes a Blueprint into a flatbuffer for serialization with the type
  // associated with the identifier.
  Span<uint8_t> Finalize(Blueprint* blueprint,
                         string_view identifier = kLegacyFileIdentifier);

 private:
  // Basic unique lock around a mutex.
  using Lock = std::unique_lock<std::mutex>;

  // System TypeId to System instance map.
  using SystemMap = std::unordered_map<TypeId, System*>;

  // ComponentDef type (hashed) to System TypeId map.
  using TypeMap = std::unordered_map<Blueprint::DefType, TypeId>;

  // ComponentDef type list used during the entity creation process.
  using TypeList = std::vector<Blueprint::DefType>;

  // Function that creates a blueprint from raw data.
  using LoadBlueprintFromDataFn =
      std::function<Optional<BlueprintTree>(const void*, size_t)>;
  using FinalizeBlueprintDataFn =
      std::function<size_t(FlatbufferWriter* writer, Blueprint* bp)>;

  // Captures the schema specific data used by the entity factory.
  struct FlatbufferConverter {
    explicit FlatbufferConverter(string_view identifier)
        : identifier(identifier) {}
    std::string identifier;
    // Function that allows the EntityFactory to create a flatbuffer from a
    // Blueprint  using client-specified EntityDef and ComponentDef classes.
    FinalizeBlueprintDataFn finalize;

    // Function that allows the EntityFactory to create Entities from raw data
    // using client-specified EntityDef and ComponentDef classes.
    LoadBlueprintFromDataFn load;

    // List of ComponentDef types used during the creation process.
    TypeList types;
  };

  // Calls System::Initialize for all Systems created by the EntityFactory.
  void InitializeSystems();

  // Creates the Loader and Finalizer functors for BlueprintDef, allowing
  // access to assets created from the build_blueprint BUILD rule.
  void InitializeBlueprintConverter();

  // Creates the TypeList from the given list of names.  The list of names must
  // end with a name that is nullptr.
  void CreateTypeList(const char* const* names, FlatbufferConverter* converter);

  // Internal versions of the above functions to avoid extra converter
  // lookups.
  template <typename EntityDef, typename ComponentDef, typename GetEntityDefFn>
  void InitializeLoader(GetEntityDefFn get_entity_def,
                        FlatbufferConverter* converter);

  template <typename EntityDef, typename ComponentDef>
  void InitializeFinalizer(FlatbufferConverter* converter);

  // Returns the index of the specified name in the TypeList, or 0 if not found.
  size_t PerformReverseTypeLookup(Blueprint::DefType name,
                                  const FlatbufferConverter* converter) const;

  // Performs the actual creation of the |entity| with the given |name| using
  // the data in the |blueprint|.  Returns true if entity was successfully
  // created, false otherwise.
  bool CreateImpl(Entity entity, const std::string& name, const void* data,
                  size_t len);

  // Performs the actual creation of the |entity| using the |blueprint|.
  // Returns true if entity was successfully created, false otherwise.
  bool CreateImpl(Entity entity, Blueprint* blueprint,
                  std::list<BlueprintTree>* children = nullptr);
  bool CreateImpl(Entity entity, BlueprintTree* blueprint);

  // Create a blueprint from asset without creating an entity.
  Optional<BlueprintTree> CreateBlueprintFromAsset(const std::string& name,
                                                   const SimpleAsset* asset);
  // Create a blueprint from data without creating an entity.
  Optional<BlueprintTree> CreateBlueprintFromData(const std::string& name,
                                                  const void* data,
                                                  size_t size);

  // Caches the System mapped to the type.
  void AddSystem(TypeId system_type, System* system);

  // Gets a System associated with a DefType.
  System* GetSystem(const Blueprint::DefType def_type);

  // Creates and stores a new FlatbufferConverter.
  FlatbufferConverter* CreateFlatbufferConverter(string_view identifier);

  // Looks up the FlatbufferConverter by its 4 letter identifier.
  // If the app only has 1 schema, that schema will be returned regardless of
  // the identifier.
  FlatbufferConverter* GetFlatbufferConverter(string_view identifier);

  // Create a blueprint tree from an entity def.
  template <typename EntityDef, typename ComponentDef>
  BlueprintTree BlueprintTreeFromEntityDef(
      const EntityDef* entity_def, const FlatbufferConverter* converter);

  // The registry is used to create and own Systems.
  Registry* registry_;

  // ResourceManager to cache loaded Entity blueprints.
  ResourceManager<SimpleAsset> blueprints_;

  // List of entity schemas that have been registered.  Most apps will only ever
  // need one converter unless they are compiled into the same binary as other
  // Lullaby applications, in which case they may need two.  One for shared
  // blueprints, one for blueprints using app specific defs.
  std::vector<std::unique_ptr<FlatbufferConverter>> converters_;

  DependencyChecker dependency_checker_;

  // Handlers for ComponentDefTs that have been registered.
  ComponentHandlers component_handlers_;

  // Map of TypeId to System instances.
  SystemMap systems_;

  // Map of ComponentDef type (hash) to System TypeIds.
  TypeMap type_map_;

  // Map of created Entities.
  BlueprintMap entity_to_blueprint_map_;

  // Autoincrementing value to generate unique Entity IDs.
  uint32_t entity_generator_;

  // Queue of Entities pending destruction.
  std::queue<Entity> pending_destroy_;

  // Mutex for ensuring thread-safe operations.
  std::mutex mutex_;

  // Default create_child_fn simply creates the child without a parent for cases
  // where there's no TransformSystem.  If the TransformSystem is used, it
  // provides it's own implementation which establishes the expected parent /
  // child relationship.
  CreateChildFn create_child_fn_ = [this](Entity parent, BlueprintTree* bpt) {
    return Create(bpt);
  };

  EntityFactory(const EntityFactory& rhs) = delete;
  EntityFactory& operator=(const EntityFactory& rhs) = delete;
};

template <typename T, typename... Args>
T* EntityFactory::CreateSystem(Args&&... args) {
  T* system = registry_->Create<T>(registry_, std::forward<Args>(args)...);
  AddSystem(GetTypeId<T>(), system);
  return system;
}

template <typename T>
T* EntityFactory::AddSystemFromRegistry() {
  T* system = registry_->Get<T>();
  AddSystem(GetTypeId<T>(), system);
  return system;
}

template <typename EntityDef, typename ComponentDef, typename GetEntityDefFn>
void EntityFactory::RegisterFlatbufferConverter(
    GetEntityDefFn get_entity_def, const char* const* component_def_names,
    string_view file_identifier) {
  FlatbufferConverter* converter = CreateFlatbufferConverter(file_identifier);
  CreateTypeList(component_def_names, converter);
  InitializeLoader<EntityDef, ComponentDef>(get_entity_def, converter);
  InitializeFinalizer<EntityDef, ComponentDef>(converter);
}

inline void EntityFactory::Initialize(const char* const* component_def_names) {
  Initialize(component_def_names, kLegacyFileIdentifier);
}
inline void EntityFactory::Initialize(const char* const* component_def_names,
                                      string_view file_identifier) {
  Initialize();
  FlatbufferConverter* converter = CreateFlatbufferConverter(file_identifier);
  CreateTypeList(component_def_names, converter);
}

template <typename EntityDef, typename ComponentDef, typename GetEntityDefFn>
void EntityFactory::Initialize(GetEntityDefFn get_entity_def,
                               const char* const* component_def_names) {
  Initialize();
  RegisterFlatbufferConverter<EntityDef, ComponentDef, GetEntityDefFn>(
      get_entity_def, component_def_names, kLegacyFileIdentifier);
}

namespace detail {
// This is the fallback version of ChildAccessor::Make for EntityDef's
// that don't have a 'children' member.
template <typename EntityDef, typename ComponentDef, typename = int>
struct ChildAccessor {
  static std::list<BlueprintTree> Children(
      const EntityDef* entity_def,
      const std::function<BlueprintTree(const EntityDef*)>& tree_fn) {
    return std::list<BlueprintTree>();
  }
};

// This version of ChildAccessor::Make is for EntityDef's that do have a
// 'children' member.
template <typename EntityDef, typename ComponentDef>
struct ChildAccessor<EntityDef, ComponentDef,
                     decltype((void)&EntityDef::children, 0)> {
  static std::list<BlueprintTree> Children(
      const EntityDef* entity_def,
      const std::function<BlueprintTree(const EntityDef*)>& tree_fn) {
    std::list<BlueprintTree> retval;
    const auto* children = entity_def->children();
    if (!children) {
      return retval;
    }

    for (int i = 0; i < static_cast<int>(children->size()); ++i) {
      const auto* entity_def = children->Get(i);
      retval.emplace_back(std::move(tree_fn(entity_def)));
    }
    return retval;
  }
};
}  // namespace detail

template <typename EntityDef, typename ComponentDef>
BlueprintTree EntityFactory::BlueprintTreeFromEntityDef(
    const EntityDef* entity_def, const FlatbufferConverter* converter) {
  const auto* components = entity_def->components();
  size_t count = components ? components->size() : 0;

  // Returns a type+flatbuffer::Table pair for a given index.  The Blueprint
  // uses this function (along with the total size) to iterate over
  // components without having to know about the internal details/structure
  // of the container containing those components.
  auto component_accessor = [this, components, converter](size_t index) {
    std::pair<Blueprint::DefType, const flatbuffers::Table*> result = {
        0, nullptr};
    if (components != nullptr && index >= 0 && index < components->size()) {
      const ComponentDef* component = components->Get(static_cast<int>(index));
      const int type = static_cast<int>(component->def_type());
      const void* data = component->def();
      const auto* table = reinterpret_cast<const flatbuffers::Table*>(data);
      const bool valid_type =
          type >= 0 && type < static_cast<int>(converter->types.size());

      if (valid_type && table != nullptr) {
        result.first = converter->types[type];
        result.second = table;
      }
    }
    return result;
  };

  auto children = detail::ChildAccessor<EntityDef, ComponentDef>::Children(
      entity_def, [this, converter](const EntityDef* def) {
        return BlueprintTreeFromEntityDef<EntityDef, ComponentDef>(def,
                                                                   converter);
      });

  return BlueprintTree(component_accessor, count, std::move(children));
}

template <typename EntityDef, typename ComponentDef, typename GetEntityDefFn>
void EntityFactory::InitializeLoader(GetEntityDefFn get_entity_def) {
  InitializeLoader<EntityDef, ComponentDef, GetEntityDefFn>(
      get_entity_def, kLegacyFileIdentifier);
}

template <typename EntityDef, typename ComponentDef, typename GetEntityDefFn>
void EntityFactory::InitializeLoader(GetEntityDefFn get_entity_def,
                                     string_view file_identifier) {
  FlatbufferConverter* converter = GetFlatbufferConverter(file_identifier);
  if (converter) {
    InitializeLoader<EntityDef, ComponentDef, GetEntityDefFn>(get_entity_def,
                                                              converter);
  }
}

template <typename EntityDef, typename ComponentDef, typename GetEntityDefFn>
void EntityFactory::InitializeLoader(GetEntityDefFn get_entity_def,
                                     FlatbufferConverter* converter) {
  converter->load = [this, get_entity_def, converter](
                        const void* data,
                        size_t len) -> Optional<BlueprintTree> {
    flatbuffers::Verifier verifier(reinterpret_cast<const uint8_t*>(data), len);
    if (verifier.VerifyBuffer<EntityDef>()) {
      const EntityDef* entity_def = get_entity_def(data);
      return BlueprintTreeFromEntityDef<EntityDef, ComponentDef>(entity_def,
                                                                 converter);
    }
    LOG(WARNING)
        << "Verification failed: Blueprint file contained invalid data.";
    return NullOpt;
  };
}

template <typename EntityDef, typename ComponentDef>
void EntityFactory::InitializeFinalizer() {
  InitializeFinalizer<EntityDef, ComponentDef>(kLegacyFileIdentifier);
}

template <typename EntityDef, typename ComponentDef>
void EntityFactory::InitializeFinalizer(string_view file_identifier) {
  FlatbufferConverter* converter = GetFlatbufferConverter(file_identifier);
  if (converter) {
    InitializeFinalizer<EntityDef, ComponentDef>(converter);
  }
}

template <typename EntityDef, typename ComponentDef>
void EntityFactory::InitializeFinalizer(FlatbufferConverter* converter) {
  using DefTypeEnum = typename std::result_of<decltype (
      &ComponentDef::def_type)(ComponentDef)>::type;

  // The Blueprint is just a container of type+flatbuffer::Table objects.  We
  // want to take that information and write it to a flatbuffer with the
  // following structure:
  //
  //   union ComponentDefType { ... }
  //   table ComponentDef { def: ComponentDefType; }
  //   table EntityDef {
  //           components: [ComponentDef];
  //           children: [EntityDef];
  //   }
  converter->finalize = [this, converter](FlatbufferWriter* writer,
                                          Blueprint* blueprint) -> size_t {
    // Create the vector of ComponentDefs.  The actual data stored by the union
    // is already "encoded" into the Blueprint.  We first need to wrap it in a
    // table, and then create a vector of those table objects.
    size_t count = 0;
    const size_t components_start = writer->StartVector();
    blueprint->ForEachComponent([&](const Blueprint& bp) {
      const Blueprint::DefType type = bp.GetLegacyDefType();
      const DefTypeEnum def_type =
          static_cast<DefTypeEnum>(PerformReverseTypeLookup(type, converter));
      const flatbuffers::Table* data = bp.GetLegacyDefData();

      // Write the ComponentDef table for this component.  The table contains
      // just a single field: the ComponentDefType union.  Internally, this
      // is stored as two fields: the "type" and the reference to the actual
      // union data.
      const size_t start = writer->StartTable();
      writer->Reference(data, ComponentDef::VT_DEF);
      writer->Scalar(&def_type, ComponentDef::VT_DEF_TYPE, 0);
      const size_t table = writer->EndTable(start);

      // Add the table as an element to the vector of ComponentDefs.
      writer->AddVectorReference(table);
      ++count;
    });
    const size_t components_end = writer->EndVector(components_start, count);

    // Write the "final" table that contains the vectors of components and
    // children.  This returns the offset to the table, not the vtable.
    const size_t table_start = writer->StartTable();
    writer->Reference(components_end, EntityDef::VT_COMPONENTS);
    const size_t table_end = writer->EndTable(table_start);
    writer->Finish(table_end, converter->identifier.c_str());
    return table_end;
  };
}

template <typename DefT>
void EntityFactory::RegisterDef(TypeId system_type) {
  auto name = component_handlers_.RegisterComponentDefT<DefT>();
  RegisterDef(system_type, name);
}

}  // namespace lull

LULLABY_SETUP_TYPEID(lull::EntityFactory);

#endif  // LULLABY_MODULES_ECS_ENTITY_FACTORY_H_

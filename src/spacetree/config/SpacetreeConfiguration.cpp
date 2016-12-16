#include "SpacetreeConfiguration.hpp"
#include "spacetree/DynamicOctree.hpp"
#include "spacetree/StaticOctree.hpp"
#include "query/FindVoxelContent.hpp"
#include "utils/xml/XMLAttribute.hpp"
#include "utils/xml/ValidatorEquals.hpp"
#include "utils/xml/ValidatorOr.hpp"
#include "utils/Globals.hpp"
#include <string>

namespace precice {
namespace spacetree {

logging::Logger SpacetreeConfiguration:: _log ( "precice::spacetree::SpacetreeConfiguration" );

//const std::string& SpacetreeConfiguration:: getTag()
//{
//  static std::string tag ( "spacetree" );
//  return tag;
//}

SpacetreeConfiguration:: SpacetreeConfiguration
(
  utils::XMLTag& parent )
:
  TAG("spacetree"),
  ATTR_NAME("name"),
  ATTR_TYPE("type"),
  VALUE_DYNAMIC_OCTREE("dynamic-octree"),
  VALUE_STATIC_OCTREE("static-octree"),
  VALUE_DYNAMIC_PEANOTREE2D("dynamic-peanotree-2D"),
  VALUE_DYNAMIC_PEANOTREE3D("dynamic-peanotree-3D"),
  _dimensions(0),
  _spacetrees()
  //_isValid(false)
{
  using namespace utils;
  XMLTag::Occurrence occ = XMLTag::OCCUR_ARBITRARY;
  std::list<XMLTag> tags;
  {
    XMLTag tag(*this, VALUE_DYNAMIC_OCTREE, occ, TAG);
    tags.push_back(tag);
  }
  {
    XMLTag tag(*this, VALUE_STATIC_OCTREE, occ, TAG);
    tags.push_back(tag);
  }
//  {
//    XMLTag tag(*this, VALUE_DYNAMIC_PEANOTREE2D, occ, TAG);
//    tags.push_back(tag);
//  }
//  {
//    XMLTag tag(*this, VALUE_DYNAMIC_PEANOTREE3D, occ, TAG);
//    tags.push_back(tag);
//  }

  utils::XMLAttribute<std::string> attrName ( ATTR_NAME );


//  utils::XMLAttribute<std::string> attrType ( ATTR_TYPE );
//  utils::ValidatorEquals<std::string> validDynamicOctree(VALUE_DYNAMIC_OCTREE);
//  utils::ValidatorEquals<std::string> validStaticOctree(VALUE_STATIC_OCTREE);
//  utils::ValidatorEquals<std::string> validDynamicPeanotree2D(VALUE_DYNAMIC_PEANOTREE2D);
//  utils::ValidatorEquals<std::string> validDynamicPeanotree3D(VALUE_DYNAMIC_PEANOTREE3D);
//  attrType.setValidator ( validDynamicOctree || validStaticOctree
//                          || validDynamicPeanotree2D || validDynamicPeanotree3D  );
//  tagSpacetree.addAttribute(attrType);


  utils::XMLAttribute<Eigen::VectorXd> attrOffset("offset");
  attrOffset.setDefaultValue (Eigen::VectorXd::Constant(3, 0));
  utils::XMLAttribute<Eigen::VectorXd> attrHalflength("halflength");
  utils::XMLAttribute<double> attrMaxMeshwidth ("max-meshwidth");

  for (XMLTag& tag : tags){
    tag.addAttribute(attrName);
    tag.addAttribute(attrOffset);
    tag.addAttribute(attrHalflength);
    tag.addAttribute(attrMaxMeshwidth);
    parent.addSubtag(tag);
  }
}

void SpacetreeConfiguration:: setDimensions
(
  int dimensions )
{
  TRACE(dimensions);
  assertion((dimensions == 2) || (dimensions == 3), dimensions);
  _dimensions = dimensions;
}

//bool SpacetreeConfiguration:: parseSubtag
//(
//  utils::XMLTag::XMLReader* xmlReader )
//{
//  utils::XMLTag tagSpacetree (TAG, utils::XMLTag::OCCUR_ONCE);
//
//  utils::XMLAttribute<std::string> attrName ( ATTR_NAME );
//  tagSpacetree.addAttribute ( attrName );
//
//  utils::XMLAttribute<std::string> attrType ( ATTR_TYPE );
//  utils::ValidatorEquals<std::string> validDynamicOctree(VALUE_DYNAMIC_OCTREE);
//  utils::ValidatorEquals<std::string> validStaticOctree(VALUE_STATIC_OCTREE);
//  utils::ValidatorEquals<std::string> validDynamicPeanotree2D(VALUE_DYNAMIC_PEANOTREE2D);
//  utils::ValidatorEquals<std::string> validDynamicPeanotree3D(VALUE_DYNAMIC_PEANOTREE3D);
//  attrType.setValidator ( validDynamicOctree || validStaticOctree
//                          || validDynamicPeanotree2D || validDynamicPeanotree3D  );
//  tagSpacetree.addAttribute(attrType);
//
//  if (_dimensions == 2){
//    using utils::Vector2D;
//    utils::XMLAttribute<Vector2D> attrOffset ("offset");
//    attrOffset.setDefaultValue (Vector2D(0.0));
//    tagSpacetree.addAttribute (attrOffset);
//
//    utils::XMLAttribute<Vector2D> attrHalflength ("halflength");
//    tagSpacetree.addAttribute (attrHalflength);
//  }
//  else {
//    using utils::Vector3D;
//    utils::XMLAttribute<Vector3D> attrOffset ("offset");
//    attrOffset.setDefaultValue (Vector3D(0.0));
//    tagSpacetree.addAttribute (attrOffset);
//
//    utils::XMLAttribute<Vector3D> attrHalflength ("halflength");
//    tagSpacetree.addAttribute (attrHalflength);
//  }
//
//  utils::XMLAttribute<double> attrMaxMeshwidth ("max-meshwidth");
//  tagSpacetree.addAttribute (attrMaxMeshwidth);
//
//  _isValid = tagSpacetree.parse ( xmlReader, *this );
//  return _isValid;
//}

const PtrSpacetree& SpacetreeConfiguration:: getSpacetree
(
  const std::string& name ) const
{
  //assertion ( _isValid );
  for ( const ConfiguredSpacetree& tree : _spacetrees ) {
    if ( tree.name == name ) {
      return tree.spacetree;
    }
  }
  ERROR("A spacetree with name \"" << name << "\" is not defined!" );
}

PtrSpacetree SpacetreeConfiguration:: getSpacetree
(
  const std::string&     type,
  const Eigen::VectorXd& offset,
  const Eigen::VectorXd& halflengths,
  double                 maxMeshwidth ) const
{
  TRACE(type, offset, halflengths, maxMeshwidth);
  assertion(_dimensions != 0);
  Spacetree* spacetree = nullptr;
  assertion ( offset.size() == halflengths.size(), offset.size(), halflengths.size() );
  bool equalHalflengths = true;
  equalHalflengths &= math::equals(halflengths(0), halflengths(1));
  if ( _dimensions == 3 ){
    equalHalflengths &= math::equals(halflengths(1), halflengths(2));
  }
  preciceCheck(equalHalflengths, "getSpacetree()", "All halflengths have to "
               << "be equal for a spacetree of type \"quad\"!");
  if (type == VALUE_DYNAMIC_OCTREE){
    spacetree = new DynamicOctree( offset, halflengths(0), maxMeshwidth);
  }
  else if (type == VALUE_STATIC_OCTREE){
    spacetree = new StaticOctree( offset, halflengths(0), maxMeshwidth);
  }
//  else if (type == VALUE_DYNAMIC_PEANOTREE2D){
//    spacetree = new DynamicPeanotree2D( offset, halflengths(0), maxMeshwidth);
//  }
//  else if (type == VALUE_DYNAMIC_PEANOTREE3D){
//    spacetree = new DynamicPeanotree3D( offset, halflengths(0), maxMeshwidth);
//  }
  else {
    ERROR("Unknown spacetree type \"" << type << "\"!");
  }
  assertion(spacetree != nullptr);
  return PtrSpacetree(spacetree);
}

void SpacetreeConfiguration:: xmlTagCallback
(
  utils::XMLTag& tag )
{
  TRACE(tag.getName() );
  if (tag.getNamespace() == TAG){
    assertion(_dimensions != 0);
    std::string name = tag.getStringAttributeValue(ATTR_NAME);
    std::string type = tag.getName();
    Eigen::VectorXd offset(_dimensions);
    Eigen::VectorXd halflengths(_dimensions);
    offset = tag.getEigenVectorXdAttributeValue("offset", _dimensions);
    halflengths = tag.getEigenVectorXdAttributeValue("halflength", _dimensions);
    double maxMeshwidth = tag.getDoubleAttributeValue("max-meshwidth");
    for (const ConfiguredSpacetree& tree : _spacetrees){
      if (tree.name == name){
        std::ostringstream stream;
        stream << "Spacetree with " << "name \"" << name << "\" is defined twice";
        throw stream.str();
      }
    }
    ConfiguredSpacetree tree;
    tree.name = name;
    tree.spacetree = getSpacetree(type, offset, halflengths, maxMeshwidth);
    _spacetrees.push_back(tree);
  }
}

}} // namespace precice, spacetree


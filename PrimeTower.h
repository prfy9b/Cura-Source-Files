//Copyright (c) 2016 Ultimaker B.V.
//CuraEngine is released under the terms of the AGPLv3 or higher.

#ifndef PRIME_TOWER_H
#define PRIME_TOWER_H

#include <vector>

#include "GCodePathConfig.h"
#include "MeshGroup.h"
#include "utils/polygon.h" // Polygons
#include "utils/polygonUtils.h"

namespace cura 
{

    
class SliceDataStorage;
class LayerPlan;
class GCodeExport;

/*!
 * Class for everything to do with the prime tower:
 * - Generating the areas.
 * - Checking up untill which height the prime tower has to be printed.
 * - Generating the paths and adding them to the layer plan.
 */
class PrimeTower
{
private:
    struct ExtrusionMoves
    {
        Polygons polygons;
        Polygons lines;
    };
    unsigned int extruder_count; //!< Number of extruders

    bool wipe_from_middle; //!< Whether to wipe on the inside of the hollow prime tower
    Point middle; //!< The middle of the prime tower

    Point post_wipe_point; //!< Location to post-wipe the unused nozzle off on

    std::vector<ExtrusionMoves> pattern_per_extruder; //!< For each extruder the pattern to print on all layers of the prime tower.
    std::vector<ExtrusionMoves> pattern_per_extruder_layer0; //!< For each extruder the pattern to print on the first layer

public:
    bool enabled; //!< Whether the prime tower is enabled.
    Polygons outer_poly; //!< The outline of the outermost prime tower.

    /*
     * In which order, from outside to inside, will we be printing the prime
     * towers for maximum strength?
     *
     * This is the spatial order from outside to inside. This is NOT the actual
     * order in time in which they are printed.
     */
    std::vector<unsigned int> extruder_order;

    /*!
     * \brief Creates a prime tower instance that will determine where and how
     * the prime tower gets printed.
     *
     * \param storage A storage where it retrieves the prime tower settings.
     */
    PrimeTower(const SliceDataStorage& storage);

    /*!
     * Generate the prime tower area to be used on each layer
     * 
     * Fills \ref PrimeTower::inner_poly and sets \ref PrimeTower::middle
     * 
     * \param storage Where to retrieve prime tower settings from
     */
    void generateGroundpoly(const SliceDataStorage& storage);

    /*!
     * Generate the area where the prime tower should be.
     * 
     * \param storage where to get settings from
     */
    void generatePaths(const SliceDataStorage& storage);

    /*!
     * Add path plans for the prime tower to the \p gcode_layer
     * 
     * \param storage where to get settings from; where to get the maximum height of the prime tower from
     * \param[in,out] gcode_layer Where to get the current extruder from; where to store the generated layer paths
     * \param prev_extruder The previous extruder with which paths were planned; from which extruder a switch was made
     * \param new_extruder The switched to extruder with which the prime tower paths should be generated.
     */
    void addToGcode(const SliceDataStorage& storage, LayerPlan& gcode_layer, const GCodeExport& gcode, const int prev_extruder, const int new_extruder) const;

    /*!
     * \brief Subtract the prime tower from the support areas in storage.
     *
     * \param storage The storage where to find the support from which to
     * subtract a prime tower.
     */
    void subtractFromSupport(SliceDataStorage& storage);

private:

    /*!
     * Find an approriate representation for the point representing the location before going to the prime tower
     * 
     * \warning This is not the actual position each time before the wipe tower
     * 
     * \param storage where to get settings from
     * \return that location
     */
    Point getLocationBeforePrimeTower(const SliceDataStorage& storage) const;

    /*!
     * \see WipeTower::generatePaths
     * 
     * Generate the extrude paths for each extruder on even and odd layers
     * Fill the ground poly with dense infill.
     * 
     * \param storage where to get settings from
     */
    void generatePaths_denseInfill(const SliceDataStorage& storage);

    /*!
     * \see PrimeTower::addToGcode
     *
     * Add path plans for the prime tower to the \p gcode_layer
     *
     * \param[in,out] gcode_layer Where to get the current extruder from; where to store the generated layer paths
     * \param extruder The extruder we just switched to, with which the prime
     * tower paths should be drawn.
     */
    void addToGcode_denseInfill(const SliceDataStorage& storage, LayerPlan& gcode_layer, const int extruder) const;
};




}//namespace cura

#endif // PRIME_TOWER_H

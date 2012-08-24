// -*- mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*-
// vi: set et ts=4 sw=4 sts=4:
/*****************************************************************************
 *   Copyright (C) 2012 by Andreas Lauser                                    *
 *                                                                           *
 *   This program is free software: you can redistribute it and/or modify    *
 *   it under the terms of the GNU General Public License as published by    *
 *   the Free Software Foundation, either version 2 of the License, or       *
 *   (at your option) any later version.                                     *
 *                                                                           *
 *   This program is distributed in the hope that it will be useful,         *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of          *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the           *
 *   GNU General Public License for more details.                            *
 *                                                                           *
 *   You should have received a copy of the GNU General Public License       *
 *   along with this program.  If not, see <http://www.gnu.org/licenses/>.   *
 *****************************************************************************/
/*!
 * \file
 *
 * \brief 1D problem with very fast injection of gas on the left.
 *
 * The velocity model is chosen in the .cc file for the problem!
 */
#ifndef DUMUX_POWERINJECTION_PROBLEM_HH
#define DUMUX_POWERINJECTION_PROBLEM_HH

#include <dumux/material/fluidmatrixinteractions/2p/regularizedvangenuchten.hh>
#include <dumux/material/fluidmatrixinteractions/2p/linearmaterial.hh>
#include <dumux/material/fluidmatrixinteractions/2p/efftoabslaw.hh>
#include <dumux/material/fluidmatrixinteractions/mp/2padapter.hh>
#include <dumux/material/fluidsystems/2pimmisciblefluidsystem.hh>
#include <dumux/material/fluidstates/immisciblefluidstate.hh>
#include <dumux/material/components/simpleh2o.hh>
#include <dumux/material/components/air.hh>
#include <dumux/boxmodels/immiscible/immiscibleproperties.hh>
#include <dumux/common/cubegridcreator.hh>

#include <dune/common/fvector.hh>

#include <iostream>

namespace Dumux {
template <class TypeTag>
class PowerInjectionProblem;

//////////
// Specify the properties for the powerInjection problem
//////////
namespace Properties {
NEW_TYPE_TAG(PowerInjectionBaseProblem);

// declare the properties specific for the power injection problem
NEW_PROP_TAG(DomainSizeX);
NEW_PROP_TAG(DomainSizeY);
NEW_PROP_TAG(DomainSizeZ);

NEW_PROP_TAG(CellsX);
NEW_PROP_TAG(CellsY);
NEW_PROP_TAG(CellsZ);

// set the GridCreator property
SET_TYPE_PROP(PowerInjectionBaseProblem, GridCreator, CubeGridCreator<TypeTag>);

// Retrieve the grid type from the grid creator
SET_TYPE_PROP(PowerInjectionBaseProblem, Grid, Dune::YaspGrid</*dim=*/1>);

// Set the problem property
SET_TYPE_PROP(PowerInjectionBaseProblem, Problem, Dumux::PowerInjectionProblem<TypeTag>);

// Set the wetting phase
SET_PROP(PowerInjectionBaseProblem, WettingPhase)
{
private:
    typedef typename GET_PROP_TYPE(TypeTag, Scalar) Scalar;
public:
    typedef Dumux::LiquidPhase<Scalar, Dumux::SimpleH2O<Scalar> > type;
};

// Set the non-wetting phase
SET_PROP(PowerInjectionBaseProblem, NonwettingPhase)
{
private:
    typedef typename GET_PROP_TYPE(TypeTag, Scalar) Scalar;
public:
    typedef Dumux::GasPhase<Scalar, Dumux::Air<Scalar> > type;
};

// Set the material Law
SET_PROP(PowerInjectionBaseProblem, MaterialLaw)
{
private:
    // define the material law which is parameterized by effective
    // saturations
    typedef typename GET_PROP_TYPE(TypeTag, Scalar) Scalar;
    typedef RegularizedVanGenuchten<Scalar> EffectiveLaw;
    // define the material law parameterized by absolute saturations
    typedef EffToAbsLaw<EffectiveLaw> TwoPMaterialLaw;
    
    typedef typename GET_PROP_TYPE(TypeTag, FluidSystem) FluidSystem;
    enum { wPhaseIdx = FluidSystem::wPhaseIdx };

public:
    typedef TwoPAdapter<wPhaseIdx, TwoPMaterialLaw> type;
};

// Write out the filter velocities for this problem
SET_BOOL_PROP(PowerInjectionBaseProblem, VtkWriteFilterVelocities, true);

// Disable gravity
SET_BOOL_PROP(PowerInjectionBaseProblem, EnableGravity, false);

// define the properties specific for the powerInjection problem
SET_SCALAR_PROP(PowerInjectionBaseProblem, DomainSizeX, 100.0);
SET_SCALAR_PROP(PowerInjectionBaseProblem, DomainSizeY, 1.0);
SET_SCALAR_PROP(PowerInjectionBaseProblem, DomainSizeZ, 1.0);

SET_INT_PROP(PowerInjectionBaseProblem, CellsX, 250);
SET_INT_PROP(PowerInjectionBaseProblem, CellsY, 1);
SET_INT_PROP(PowerInjectionBaseProblem, CellsZ, 1);
}

/*!
 * \ingroup ImmiscibleBoxProblems
 * \brief 1D Problem with very fast injection of gas on the left.
 *
 * The velocity model is chosen in the .cc file for the problem!
 */
template <class TypeTag>
class PowerInjectionProblem
    : public GET_PROP_TYPE(TypeTag, BaseProblem)
{
    typedef typename GET_PROP_TYPE(TypeTag, BaseProblem) ParentType;

    typedef typename GET_PROP_TYPE(TypeTag, Scalar) Scalar;
    typedef typename GET_PROP_TYPE(TypeTag, GridView) GridView;
    typedef typename GET_PROP_TYPE(TypeTag, Indices) Indices;
    typedef typename GET_PROP_TYPE(TypeTag, FluidSystem) FluidSystem;
    typedef typename GET_PROP_TYPE(TypeTag, WettingPhase) WettingPhase;
    typedef typename GET_PROP_TYPE(TypeTag, NonwettingPhase) NonwettingPhase;
    typedef typename GET_PROP_TYPE(TypeTag, PrimaryVariables) PrimaryVariables;
    typedef typename GET_PROP_TYPE(TypeTag, TimeManager) TimeManager;

    enum {
        // number of phases
        numPhases = FluidSystem::numPhases,

        // phase indices
        wPhaseIdx = FluidSystem::wPhaseIdx,
        nPhaseIdx = FluidSystem::nPhaseIdx,

        // equation indices
        contiNEqIdx = Indices::conti0EqIdx + nPhaseIdx,

        // Grid and world dimension
        dim = GridView::dimension,
        dimWorld = GridView::dimensionworld
    };

    typedef typename GET_PROP_TYPE(TypeTag, RateVector) RateVector;
    typedef typename GET_PROP_TYPE(TypeTag, BoundaryRateVector) BoundaryRateVector;

    typedef typename GET_PROP_TYPE(TypeTag, MaterialLaw) MaterialLaw;
    typedef typename GET_PROP_TYPE(TypeTag, MaterialLawParams) MaterialLawParams;

    typedef typename GridView::ctype CoordScalar;
    typedef Dune::FieldVector<CoordScalar, dimWorld> GlobalPosition;

    typedef Dune::FieldMatrix<Scalar, dimWorld, dimWorld> DimMatrix;

public:
    /*!
     * \brief The constructor
     *
     * \param timeManager The time manager
     * \param gridView The grid view
     */
    PowerInjectionProblem(TimeManager &timeManager)
        : ParentType(timeManager, GET_PROP_TYPE(TypeTag, GridCreator)::grid().leafView())
    {
        eps_ = 3e-6;
        FluidSystem::init();

        temperature_ = 273.15 + 20; // -> 20°C
        
        // parameters for the Van Genuchten law
        // alpha and n
        materialParams_.setVgAlpha(0.00045);
        materialParams_.setVgN(7.3);

        K_ = this->toDimMatrix_(9.05e-8);

        setupInitialFluidState_();
    }

    /*!
     * \name Soil parameters
     */
    // \{

    /*!
     * \brief Intrinsic permability
     *
     * \param element The current element
     * \param fvElemGeom The current finite volume geometry of the element
     * \param scvIdx The index of the sub-control volume.
     * \return Intrinsic permeability
     */
    template <class Context>
    const DimMatrix &intrinsicPermeability(const Context &context, int spaceIdx, int timeIdx) const
    { return K_; }

    /*!
     * \brief Returns the Ergun coefficient for the Forchheimer velocity model.
     */
    template <class Context>
    Scalar ergunCoefficient(const Context &context, int spaceIdx, int timeIdx) const
    { return 0.05; }

    /*!
     * \brief Porosity
     */
    template <class Context>
    Scalar porosity(const Context &context, int spaceIdx, int timeIdx) const
    { return 0.8; }

    /*!
     * \brief Parameters of the  constitutive relationships (kr(Sw), pc(Sw), etc.).
     */
    template <class Context>
    const MaterialLawParams& materialLawParams(const Context &context, int spaceIdx, int timeIdx) const
    { return materialParams_; }

    /*!
     * \brief Returns the temperature within the domain.
     */
    template <class Context>
    Scalar temperature(const Context &context,
                       int spaceIdx, int timeIdx) const
    { return temperature_; }

    // \}

    /*!
     * \name Auxiliary methods
     */
    // \{

    /*!
     * \brief The problem name.
     *
     * This is used as a prefix for files generated by the simulation.
     */
    const char *name() const
    { return "powerinjection"; }

    /*!
     * \brief Called directly after the time integration.
     */
    void postTimeStep()
    {
        // Calculate storage terms
        PrimaryVariables storage;
        this->model().globalStorage(storage);

        // Write mass balance information for rank 0
        if (this->gridView().comm().rank() == 0) {
            std::cout<<"Storage: " << storage << std::endl;
        }
    }
    // \}

    /*!
     * \name Boundary conditions
     */
    // \{

    /*!
     * \brief Evaluate the boundary conditions for a boundary segment.
     */
    template <class Context>
    void boundary(BoundaryRateVector &values,
                  const Context &context,
                  int spaceIdx, int timeIdx) const
    {
        const GlobalPosition &pos = context.pos(spaceIdx, timeIdx);

        if (onLeftBoundary_(pos)) {
            RateVector massRate(0.0);
            massRate = 0.0;
            massRate[contiNEqIdx] = -1.00; // kg / (m^2 * s)

            // impose a forced flow boundary
            values.setMassRate(massRate);
        }
        else  {
            // free flow boundary with initial condition on the right
            values.setFreeFlow(context, spaceIdx, timeIdx, initialFluidState_);
        }
            
    }

    // \}

    /*!
     * \name Volume terms
     */
    // \{

    /*!
     * \brief Evaluate the initial value for a control volume.
     *
     * \param values The initial values for the primary variables
     * \param pos The center of the finite volume which ought to be set.
     *
     * For this method, the \a values parameter stores primary
     * variables.
     */
    template <class Context>
    void initial(PrimaryVariables &values,
                 const Context &context,
                 int spaceIdx, int timeIdx) const
    {
        // assign the primary variables
        values.assignNaive(initialFluidState_);
    }


    template <class Context>
    void source(RateVector &values,
                const Context &context,
                int spaceIdx, int timeIdx) const
    { values = 0; }
    // \}

private:   
    bool onLeftBoundary_(const GlobalPosition &pos) const
    { return pos[0] < this->bboxMin()[0] + eps_; }

    bool onRightBoundary_(const GlobalPosition &pos) const
    { return pos[0] > this->bboxMax()[0] - eps_; }

    void setupInitialFluidState_()
    {
        initialFluidState_.setTemperature(temperature_);

        Scalar Sw = 1.0;
        initialFluidState_.setSaturation(wPhaseIdx, Sw);
        initialFluidState_.setSaturation(nPhaseIdx, 1 - Sw);

        Scalar p = 1e5;
        initialFluidState_.setPressure(wPhaseIdx, p);
        initialFluidState_.setPressure(nPhaseIdx, p);
    }

    DimMatrix K_;
    MaterialLawParams materialParams_;

    ImmiscibleFluidState<Scalar, FluidSystem> initialFluidState_;
    Scalar temperature_;
    Scalar eps_;
};

} //end namespace

#endif
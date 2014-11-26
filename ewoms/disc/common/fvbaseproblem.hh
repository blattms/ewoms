/*
  Copyright (C) 2008-2013 by Andreas Lauser
  Copyright (C) 2010-2011 by Markus Wolff

  This file is part of the Open Porous Media project (OPM).

  OPM is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 2 of the License, or
  (at your option) any later version.

  OPM is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with OPM.  If not, see <http://www.gnu.org/licenses/>.
*/
/*!
 * \file
 *
 * \copydoc Ewoms::FvBaseProblem
 */
#ifndef EWOMS_FV_BASE_PROBLEM_HH
#define EWOMS_FV_BASE_PROBLEM_HH

#include "fvbaseproperties.hh"

#include <ewoms/io/vtkmultiwriter.hh>
#include <ewoms/io/restart.hh>
#include <dune/common/fvector.hh>

#include <iostream>
#include <limits>
#include <string>

namespace Ewoms {

/*!
 * \ingroup Discretization
 *
 * \brief Base class for all problems which use a finite volume spatial discretization.
 *
 * \note All quantities are specified assuming a threedimensional world. Problems
 *       discretized using 2D grids are assumed to be extruded by \f$1 m\f$ and 1D grids
 *       are assumed to have a cross section of \f$1m \times 1m\f$.
 */
template<class TypeTag>
class FvBaseProblem
{
private:
    typedef typename GET_PROP_TYPE(TypeTag, Problem) Implementation;
    typedef typename GET_PROP_TYPE(TypeTag, GridView) GridView;

    static const int vtkOutputFormat = GET_PROP_VALUE(TypeTag, VtkOutputFormat);
    typedef Ewoms::VtkMultiWriter<GridView, vtkOutputFormat> VtkMultiWriter;

    typedef typename GET_PROP_TYPE(TypeTag, Model) Model;
    typedef typename GET_PROP_TYPE(TypeTag, Scalar) Scalar;
    typedef typename GET_PROP_TYPE(TypeTag, Simulator) Simulator;
    typedef typename GET_PROP_TYPE(TypeTag, ThreadManager) ThreadManager;
    typedef typename GET_PROP_TYPE(TypeTag, NewtonMethod) NewtonMethod;

    typedef typename GET_PROP_TYPE(TypeTag, VertexMapper) VertexMapper;
    typedef typename GET_PROP_TYPE(TypeTag, ElementMapper) ElementMapper;

    typedef typename GET_PROP_TYPE(TypeTag, RateVector) RateVector;
    typedef typename GET_PROP_TYPE(TypeTag, BoundaryRateVector) BoundaryRateVector;
    typedef typename GET_PROP_TYPE(TypeTag, PrimaryVariables) PrimaryVariables;
    typedef typename GET_PROP_TYPE(TypeTag, Constraints) Constraints;

    enum {
        dim = GridView::dimension,
        dimWorld = GridView::dimensionworld
    };

    typedef typename GridView::template Codim<0>::Entity Element;
    typedef typename GridView::template Codim<dim>::Entity Vertex;
    typedef typename GridView::template Codim<dim>::Iterator VertexIterator;

    typedef typename GridView::Grid::ctype CoordScalar;
    typedef Dune::FieldVector<CoordScalar, dimWorld> GlobalPosition;

    // copying a problem is not a good idea
    FvBaseProblem(const FvBaseProblem &) = delete;

public:
    /*!
     * \copydoc Doxygen::defaultProblemConstructor
     *
     * \param simulator The time manager of the simulation
     * \param gridView The view on the DUNE grid which ought to be
     *                 used (normally the leaf grid view)
     */
    FvBaseProblem(Simulator &simulator)
        : gridView_(simulator.gridView())
        , elementMapper_(gridView_)
        , vertexMapper_(gridView_)
        , boundingBoxMin_(std::numeric_limits<double>::max())
        , boundingBoxMax_(-std::numeric_limits<double>::max())
        , simulator_(simulator)
        , defaultVtkWriter_(0)
    {
        // calculate the bounding box of the local partition of the grid view
        VertexIterator vIt = gridView_.template begin<dim>();
        const VertexIterator vEndIt = gridView_.template end<dim>();
        for (; vIt!=vEndIt; ++vIt) {
            for (int i=0; i<dim; i++) {
                boundingBoxMin_[i] = std::min(boundingBoxMin_[i], vIt->geometry().corner(0)[i]);
                boundingBoxMax_[i] = std::max(boundingBoxMax_[i], vIt->geometry().corner(0)[i]);
            }
        }

        // communicate to get the bounding box of the whole domain
        for (int i = 0; i < dim; ++i) {
            boundingBoxMin_[i] = gridView_.comm().min(boundingBoxMin_[i]);
            boundingBoxMax_[i] = gridView_.comm().max(boundingBoxMax_[i]);
        }

        if (enableVtkOutput_())
            defaultVtkWriter_ = new VtkMultiWriter(gridView_, asImp_().name());
    }

    /*!
     * \brief Registers all available parameters for the problem and
     *        the model.
     */
    static void registerParameters()
    {
        Model::registerParameters();
        EWOMS_REGISTER_PARAM(TypeTag, Scalar, MaxTimeStepSize,
                             "The maximum size to which all time steps are limited to [s]");
        EWOMS_REGISTER_PARAM(TypeTag, Scalar, MinTimeStepSize,
                             "The minimum size to which all time steps are limited to [s]");
        EWOMS_REGISTER_PARAM(TypeTag, unsigned, MaxTimeStepDivisions,
                             "The maximum number of divisions by two of the timestep size "
                             "before the simulation bails out");
    }

    /*!
     * \brief Called by the Ewoms::Simulator in order to initialize the problem.
     *
     * If you overload this method don't forget to call ParentType::finishInit()
     */
    void finishInit()
    {
        assembleTime_ = 0.0;
        solveTime_ = 0.0;
        updateTime_ = 0.0;
    }

    /*!
     * \brief Returns the total wall time spend on solving the
     *        system [s].
     */
    Scalar solveTime() const
    { return solveTime_; }

    /*!
     * \brief Returns the total wall time spend on updating the
     *        iterative solutions [s].
     */
    Scalar updateTime() const
    { return updateTime_; }

    /*!
     * \brief Evaluate the boundary conditions for a boundary segment.
     *
     * \param values Stores the fluxes over the boundary segment.
     * \param context The object representing the execution context from
     *                which this method is called.
     * \param spaceIdx The local index of the spatial entity which represents the boundary segment.
     * \param timeIdx The index used for the time discretization
     */
    template <class Context>
    void boundary(BoundaryRateVector &values,
                  const Context &context,
                  int spaceIdx, int timeIdx) const
    { OPM_THROW(std::logic_error, "Problem does not provide a boundary() method"); }

    /*!
     * \brief Evaluate the constraints for a control volume.
     *
     * \param constraints Stores the values of the primary variables at a
     *                    given spatial and temporal location.
     * \param context The object representing the execution context from
     *                which this method is called.
     * \param spaceIdx The local index of the spatial entity which represents the boundary segment.
     * \param timeIdx The index used for the time discretization
     */
    template <class Context>
    void constraints(Constraints &constraints,
                     const Context &context,
                     int spaceIdx, int timeIdx) const
    { OPM_THROW(std::logic_error, "Problem does not provide a constraints() method"); }

    /*!
     * \brief Evaluate the source term for all phases within a given
     *        sub-control-volume.
     *
     * \param rate Stores the values of the volumetric creation/anihilition
     *             rates of the conserved quantities.
     * \param context The object representing the execution context from which
     *                this method is called.
     * \param spaceIdx The local index of the spatial entity which represents
     *                 the boundary segment.
     * \param timeIdx The index used for the time discretization
     */
    template <class Context>
    void source(RateVector &rate,
                const Context &context,
                int spaceIdx, int timeIdx) const
    { OPM_THROW(std::logic_error, "Problem does not provide a source() method"); }

    /*!
     * \brief Evaluate the initial value for a control volume.
     *
     * \param values Stores the primary variables.
     * \param context The object representing the execution context from which
     *                this method is called.
     * \param spaceIdx The local index of the spatial entity which represents
     *                 the boundary segment.
     * \param timeIdx The index used for the time discretization
     */
    template <class Context>
    void initial(PrimaryVariables &values,
                 const Context &context,
                 int spaceIdx, int timeIdx) const
    { OPM_THROW(std::logic_error, "Problem does not provide a initial() method"); }

    /*!
     * \brief Return how much the domain is extruded at a given sub-control volume.
     *
     * This means the factor by which a lower-dimensional (1D or 2D)
     * entity needs to be expanded to get a full dimensional cell. The
     * default is 1.0 which means that 1D problems are actually
     * thought as pipes with a cross section of 1 m^2 and 2D problems
     * are assumed to extend 1 m to the back.
     *
     * \param context The object representing the execution context from which
     *                this method is called.
     * \param spaceIdx The local index of the spatial entity which represents
     *                 the boundary segment.
     * \param timeIdx The index used for the time discretization
     */
    template <class Context>
    Scalar extrusionFactor(const Context &context,
                           int spaceIdx, int timeIdx) const
    { return asImp_().extrusionFactor(); }

    Scalar extrusionFactor() const
    { return 1.0; }

    /*!
     * \brief Called at the beginning of an simulation episode.
     */
    void beginEpisode()
    { }

    /*!
     * \brief Called by the simulator before each time integration.
     */
    void beginTimeStep()
    { }

    /*!
     * \brief Called by the simulator before each Newton-Raphson iteration.
     */
    void beginIteration()
    { }

    /*!
     * \brief Called by the simulator after each Newton-Raphson update.
     */
    void endIteration()
    { }

    /*!
     * \brief Called by the simulator after each time integration.
     *
     * This method is intended to do some post processing of the
     * solution. (e.g., some additional output)
     */
    void endTimeStep()
    { }

    /*!
     * \brief Called when the end of an simulation episode is reached.
     *
     * Typically, a new episode is started in this method.
     */
    void endEpisode()
    {
        std::cerr << "The end of an episode is reached, but the problem "
                  << "does not override the endEpisode() method. "
                  << "Doing nothing!\n";
    }

    /*!
     * \brief Called after the simulation has been run sucessfully.
     */
    void finalize()
    {
        const auto& timer = simulator().timer();
        Scalar realTime = timer.realTimeElapsed();
        Scalar localCpuTime = timer.cpuTimeElapsed();
        Scalar globalCpuTime = timer.globalCpuTimeElapsed();
        int numProcesses = this->gridView().comm().size();
        int threadsPerProcess = ThreadManager::maxThreads();
        if (gridView().comm().rank() == 0) {
            std::cout << std::setprecision(3)
                      << "Simulation of problem '" << asImp_().name() << "' finished.\n"
                      << "\n"
                      << "-------------- Timing receipt --------------\n"
                      << "  Wall-clock time: "<< Simulator::humanReadableTime(realTime, /*isAmendment=*/false) << "\n"
                      << "  First process' CPU time: " <<  Simulator::humanReadableTime(localCpuTime, /*isAmendment=*/false) << "\n"
                      << "  Number of processes: " << numProcesses << "\n"
                      << "  Threads per processes: " << threadsPerProcess << "\n"
                      << "  Total CPU time: " << Simulator::humanReadableTime(globalCpuTime, /*isAmendment=*/false) << "\n"
                      << "  Setup time: "<< Simulator::humanReadableTime(simulator().setupTime(), /*isAmendment=*/false)
                      << ", " << simulator().setupTime()/realTime*100 << "%\n"
                      << "  Linearization time: "<< Simulator::humanReadableTime(assembleTime_, /*isAmendment=*/false)
                      << ", " << assembleTime_/realTime*100 << "%\n"
                      << "  Linear solve time: " << Simulator::humanReadableTime(solveTime_, /*isAmendment=*/false)
                      << ", " << solveTime_/realTime*100 << "%\n"
                      << "  Newton update time: " << Simulator::humanReadableTime(updateTime_, /*isAmendment=*/false)
                      << ", " << updateTime_/realTime*100 << "%\n"
                      << "\n"
                      << "Note 1: If not stated otherwise, all times\n"
                      << "        are wall clock times\n"
                      << "Note 2: Taxes and administrative overhead\n"
                      << "        are "
                      << (realTime - (assembleTime_+solveTime_+updateTime_))/realTime*100
                      << "% of total execution time.\n"
                      << "\n"
                      << "Our simulation hours are 24/7. Thank you for\n"
                      << "choosing us.\n"
                      << "--------------------------------------------\n"
                      << "\n"
                      << std::flush;
        }
    }

    /*!
     * \brief Called by Ewoms::Simulator in order to do a time
     *        integration on the model.
     */
    void timeIntegration()
    {
        int maxFails = EWOMS_GET_PARAM(TypeTag, unsigned, MaxTimeStepDivisions);
        Scalar minTimeStepSize = EWOMS_GET_PARAM(TypeTag, Scalar, MinTimeStepSize);

        // if the time step size of the simulator is smaller than
        // the specified minimum size and we're not going to finish
        // the simulation or an episode, try with the minimum size.
        if (simulator().timeStepSize() < minTimeStepSize &&
            !simulator().episodeWillBeOver() &&
            !simulator().willBeFinished())
        {
            simulator().setTimeStepSize(minTimeStepSize);
        }

        for (int i = 0; i < maxFails; ++i) {
            bool converged = model().update(newtonMethod());

            assembleTime_ += newtonMethod().assembleTime();
            solveTime_ += newtonMethod().solveTime();
            updateTime_ += newtonMethod().updateTime();

            if (converged)
                return;

            Scalar dt = simulator().timeStepSize();
            Scalar nextDt = dt / 2;
            if (nextDt < minTimeStepSize)
                break; // give up: we can't make the time step smaller anymore!
            simulator().setTimeStepSize(nextDt);

            // update failed
            if (gridView().comm().rank() == 0)
                std::cout << "Newton solver did not converge with "
                          << "dt=" << dt << " seconds. Retrying with time step of "
                          << nextDt << " seconds\n" << std::flush;
        }

        OPM_THROW(std::runtime_error,
                   "Newton solver didn't converge after "
                   << maxFails << " time-step divisions. dt="
                   << simulator().timeStepSize());
    }

    /*!
     * \brief Called by Ewoms::Simulator whenever a solution for a
     *        time step has been computed and the simulation time has
     *        been updated.
     */
    Scalar nextTimeStepSize()
    {
        return std::min(EWOMS_GET_PARAM(TypeTag, Scalar, MaxTimeStepSize),
                        newtonMethod().suggestTimeStepSize(simulator().timeStepSize()));
    }

    /*!
     * \brief Returns true if a restart file should be written to
     *        disk.
     *
     * The default behavior is to write one restart file every 10 time
     * steps. This method should be overwritten by the
     * implementation if the default behavior is deemed insufficient.
     */
    bool shouldWriteRestartFile() const
    {
        return simulator().timeStepIndex() > 0 &&
            (simulator().timeStepIndex() % 10 == 0);
    }

    /*!
     * \brief Returns true if the current solution should be written to
     *        disk (i.e. as a VTK file)
     *
     * The default behavior is to write out the solution for every
     * time step. This method is should be overwritten by the
     * implementation if the default behavior is deemed insufficient.
     */
    bool shouldWriteOutput() const
    { return true; }

    /*!
     * \brief Called by the simulator after everything which can be
     *        done about the current time step is finished and the
     *        model should be prepared to do the next time integration.
     */
    void advanceTimeLevel()
    { model().advanceTimeLevel(); }

    /*!
     * \brief The problem name.
     *
     * This is used as a prefix for files generated by the simulation.
     * It is highly recommend to overwrite this method in the concrete
     * problem which is simulated.
     */
    std::string name() const
    { return "sim"; }

    /*!
     * \brief The GridView which used by the problem.
     */
    const GridView &gridView() const
    { return gridView_; }

    /*!
     * \brief The coordinate of the corner of the GridView's bounding
     *        box with the smallest values.
     */
    const GlobalPosition &boundingBoxMin() const
    { return boundingBoxMin_; }

    /*!
     * \brief The coordinate of the corner of the GridView's bounding
     *        box with the largest values.
     */
    const GlobalPosition &boundingBoxMax() const
    { return boundingBoxMax_; }

    /*!
     * \brief Returns the mapper for vertices to indices.
     */
    const VertexMapper &vertexMapper() const
    { return vertexMapper_; }

    /*!
     * \brief Returns the mapper for elements to indices.
     */
    const ElementMapper &elementMapper() const
    { return elementMapper_; }

    /*!
     * \brief Returns Simulator object used by the simulation
     */
    Simulator &simulator()
    { return simulator_; }

    /*!
     * \copydoc simulator()
     */
    const Simulator &simulator() const
    { return simulator_; }

    /*!
     * \brief Returns numerical model used for the problem.
     */
    Model &model()
    { return simulator_.model(); }

    /*!
     * \copydoc model()
     */
    const Model &model() const
    { return simulator_.model(); }

    /*!
     * \brief Returns object which implements the Newton method.
     */
    NewtonMethod &newtonMethod()
    { return model().newtonMethod(); }

    /*!
     * \brief Returns object which implements the Newton method.
     */
    const NewtonMethod &newtonMethod() const
    { return model().newtonMethod(); }
    // \}

    /*!
     * \brief This method writes the complete state of the problem
     *        to the harddisk.
     *
     * The file will start with the prefix returned by the name()
     * method, has the current time of the simulation clock in it's
     * name and uses the extension <tt>.ers</tt>. (Ewoms ReStart
     * file.)  See Ewoms::Restart for details.
     *
     * \tparam Restarter The serializer type
     *
     * \param res The serializer object
     */
    template <class Restarter>
    void serialize(Restarter &res)
    {
        if (enableVtkOutput_())
            defaultVtkWriter_->serialize(res);
    }

    /*!
     * \brief This method restores the complete state of the problem
     *        from disk.
     *
     * It is the inverse of the serialize() method.
     *
     * \tparam Restarter The deserializer type
     *
     * \param res The deserializer object
     */
    template <class Restarter>
    void deserialize(Restarter &res)
    {
        if (enableVtkOutput_())
            defaultVtkWriter_->deserialize(res);
    }

    /*!
     * \brief Write the relevant secondary variables of the current
     *        solution into an VTK output file.
     *
     * \param verbose Specify if a message should be printed whenever a file is written
     */
    void writeOutput(bool verbose = true)
    {
        if (verbose && gridView().comm().rank() == 0)
            std::cout << "Writing visualization results for the current time step.\n"
                      << std::flush;

        // calculate the time _after_ the time was updated
        Scalar t = simulator().time() + simulator().timeStepSize();

        if (enableVtkOutput_())
            defaultVtkWriter_->beginWrite(t);

        model().prepareOutputFields();

        if (enableVtkOutput_()) {
            model().appendOutputFields(*defaultVtkWriter_);
            defaultVtkWriter_->endWrite();
        }
    }

    /*!
     * \brief Method to retrieve the VTK writer which should be used
     *        to write the default ouput after each time step to disk.
     */
    VtkMultiWriter &defaultVtkWriter() const
    { return defaultVtkWriter_; }

private:
    bool enableVtkOutput_() const
    { return EWOMS_GET_PARAM(TypeTag, bool, EnableVtkOutput); }

    //! Returns the implementation of the problem (i.e. static polymorphism)
    Implementation &asImp_()
    { return *static_cast<Implementation *>(this); }

    //! \copydoc asImp_()
    const Implementation &asImp_() const
    { return *static_cast<const Implementation *>(this); }

    // Grid management stuff
    const GridView gridView_;
    ElementMapper elementMapper_;
    VertexMapper vertexMapper_;
    GlobalPosition boundingBoxMin_;
    GlobalPosition boundingBoxMax_;

    // Attributes required for the actual simulation
    Simulator &simulator_;
    mutable VtkMultiWriter *defaultVtkWriter_;

    // CPU time keeping
    Scalar assembleTime_;
    Scalar solveTime_;
    Scalar updateTime_;
};

} // namespace Ewoms

#endif

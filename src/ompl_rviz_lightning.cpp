/*********************************************************************
 * Software License Agreement (BSD License)
 *
 *  Copyright (c) 2014, University of Colorado, Boulder
 *  Copyright (c) 2012, Willow Garage, Inc.
 *  All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above
 *     copyright notice, this list of conditions and the following
 *     disclaimer in the documentation and/or other materials provided
 *     with the distribution.
 *   * Neither the name of the Willow Garage nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 *  FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 *  COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 *  BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 *  LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 *  CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 *  LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 *  ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *  POSSIBILITY OF SUCH DAMAGE.
 *********************************************************************/

/*
  Author: Dave Coleman <dave@dav.ee>
  Desc:   Visualize planning with OMPL in Rviz
*/

// ROS
#include <ros/ros.h>
#include <ros/package.h> // for getting file path for loading images

// Display in Rviz tool
#include <ompl_rviz_viewer/ompl_rviz_viewer.h>
#include <ompl_rviz_viewer/two_dimensional_validity_checker.h>

// OMPL
#include <ompl/tools/lightning/Lightning.h>
#include <ompl/geometric/planners/rrt/RRT.h>
#include <ompl/geometric/planners/rrt/TRRT.h>
#include <ompl/base/PlannerTerminationCondition.h>

namespace ob = ompl::base;
namespace ot = ompl::tools;
namespace og = ompl::geometric;

namespace ompl_rviz_viewer
{

/**
 * \brief Lightning Planning Class
 */
class OmplRvizLightning
{

private:

    // Save the experience setup until the program ends so that the planner data is not lost
    ot::LightningPtr lightning_setup_;

    // Cost in 2D
    ompl::base::CostMap2DOptimizationObjectivePtr cost_map_;

    // The visual tools for interfacing with Rviz
    ompl_rviz_viewer::OmplRvizViewerPtr viewer_;

    // The number of dimensions - always 2 for images
    static const unsigned int DIMENSIONS = 2;

    // The state space we plan in
    ob::StateSpacePtr space_;

    // Flag for determining amount of debug output to show
    bool verbose_;

public:

    /**
     * \brief Constructor
     */
    OmplRvizLightning(bool verbose)
        : verbose_(verbose)
    {
        ROS_INFO_STREAM( "OMPL version: " << OMPL_VERSION );

        // Load the tool for displaying in Rviz
        viewer_.reset(new ompl_rviz_viewer::OmplRvizViewer(verbose_));

        // Construct the state space we are planning in
        space_.reset( new ob::RealVectorStateSpace( DIMENSIONS ));

        // Define an experience setup class
        lightning_setup_ = ot::LightningPtr( new ot::Lightning(space_) );

        // Load the cost map
        cost_map_.reset(new ompl::base::CostMap2DOptimizationObjective( lightning_setup_->getSpaceInformation() ));
    }

    /**
     * \brief Deconstructor
     */
    ~OmplRvizLightning()
    {
    }

    void loadCostMapImage( std::string image_path )
    {
        cost_map_->loadImage(image_path);

        // Set the bounds for the R^2
        ob::RealVectorBounds bounds( DIMENSIONS );
        bounds.setLow( 0 ); // both dimensions start at 0
        bounds.setHigh( 0, cost_map_->image_->x - 1 ); // allow for non-square images
        bounds.setHigh( 1, cost_map_->image_->y - 1 ); // allow for non-square images
        space_->as<ob::RealVectorStateSpace>()->setBounds( bounds );

        // Render the map ---------------------------------------------------
        viewer_->displayTriangles(cost_map_->image_, cost_map_->cost_);
        ros::Duration(0.1).sleep();
    }

    bool plan(int runs, bool use_recall)
    {
        // Set the setup planner (TRRT)
        og::TRRT *trrt = new og::TRRT( lightning_setup_->getSpaceInformation() );
        //og::RRT *trrt = new og::RRT( lightning_setup_->getSpaceInformation() );

        lightning_setup_->setPlanner(ob::PlannerPtr(trrt));

        // Start and Goal State ---------------------------------------------
        ob::PlannerStatus solved;

        for (std::size_t i = 0; i < runs; ++i)
        {
            // Check if user wants to shutdown
            if (!ros::ok())
            {
                ROS_WARN_STREAM_NAMED("plan","Terminating early");
                break;
            }

            ROS_INFO_STREAM_NAMED("plan","Planning #" << i << " out of " << runs << " ------------------------------------");

            // Clear all planning data. This only includes data generated by motion plan computation.
            // Planner settings, start & goal states are not affected.
            if (i) // skip first run
                lightning_setup_->clear();

            // Set state validity checking for this space
            lightning_setup_->setStateValidityChecker( ob::StateValidityCheckerPtr( new ob::TwoDimensionalValidityChecker(
                        lightning_setup_->getSpaceInformation(), cost_map_->cost_, cost_map_->max_cost_threshold_ ) ) );

            // Setup the optimization objective to use the 2d cost map
            lightning_setup_->setOptimizationObjective(cost_map_);

            // Create the termination condition
            ob::PlannerTerminationCondition ptc = ob::timedPlannerTerminationCondition( 10.0, 0.1 );

            // Create start and goal space
            ob::ScopedState<> start(space_);
            ob::ScopedState<> goal(space_);
            chooseStartGoal(start, goal);

            // Visualize on map
            viewer_->showState(start, cost_map_->cost_, GREEN);
            viewer_->showState(goal,  cost_map_->cost_, RED);

            // set the start and goal states
            lightning_setup_->setStartAndGoalStates(start, goal);

            // Setup -----------------------------------------------------------

            // Auto setup parameters (optional actually)
            lightning_setup_->setup();
            lightning_setup_->enableRecall(use_recall);

            // The interval in which obstacles are checked for between states
            // lightning_setup_->getSpaceInformation()->setStateValidityCheckingResolution(0.005);

            // Debug - this call is optional, but we put it in to get more output information
            //lightning_setup_->print();

            // Solve -----------------------------------------------------------

            // attempt to solve the problem within x seconds of planning time
            solved = lightning_setup_->solve( ptc );

            if (solved)
            {
                ROS_INFO("Solution Found");
                if (runs == 1)
                {
                    // display all the aspects of the solution
                    displayPlannerData(false);
                }
                else
                {
                    // only display the paths
                    displayPlannerData(true);
                }
            }
            else
            {
                ROS_ERROR("No Solution Found");
            }
        }

        if (!lightning_setup_->saveIfChanged())
            ROS_ERROR("Unable to save experience database");

        return solved;
    }

    void chooseStartGoal(ob::ScopedState<>& start, ob::ScopedState<>& goal)
    {
        if( false ) // choose completely random state
        {
            start.random();
            goal.random();            
        }
        else if (false) // Manually set the start location
        {
            start[0] = 95;
            start[1] = 10;
            goal[0] = 40;
            goal[1] = 300;
        }
        else // Randomly sample around two states
        {
            ROS_INFO_STREAM_NAMED("temp","Sampling start and goal around two center points");

            ob::ScopedState<> start_area(space_);
            start_area[0] = 95;
            start_area[1] = 90;

            ob::ScopedState<> goal_area(space_);
            goal_area[0] = 40;
            goal_area[1] = 14;

            // Check these hard coded values against varying image sizes
            if (!space_->satisfiesBounds(start_area.get()) || !space_->satisfiesBounds(goal_area.get()))
            {
                ROS_ERROR_STREAM_NAMED("chooseStartGoal:","State does not satisfy bounds");
                exit(-1);
                return;
            }
            
            // Choose the distance to sample around
            double maxExtent = lightning_setup_->getSpaceInformation()->getMaximumExtent();            
            double distance = maxExtent * 0.05;
            ROS_INFO_STREAM_NAMED("temp","Distance is " << distance << " from max extent " << maxExtent);

            // Create sampler
            ob::StateSamplerPtr sampler = lightning_setup_->getSpaceInformation()->allocStateSampler();

            std::cout << "debug " << start_area << std::endl;

            sampler->sampleUniformNear(start.get(), start_area.get(), distance); // samples (near + distance, near - distance)
            sampler->sampleUniformNear(goal.get(), goal_area.get(), distance);

            std::cout << "debug after " << start << std::endl;

            // Show the new sampled points
            viewer_->displaySampleRegion(start_area, distance, cost_map_->cost_);
            viewer_->displaySampleRegion(goal_area, distance, cost_map_->cost_);
        }
    }

    /**
     * \brief Show the planner data in Rviz
     * \param planner_data
     * \param just_path - if true, do not display the search tree/graph or the samples
     */
    void displayPlannerData(bool just_path)
    {
        // Get information about the exploration data structure the motion planner used. Used later in visualizing
        const ob::PlannerDataPtr planner_data( new ob::PlannerData( lightning_setup_->getSpaceInformation() ) );
        lightning_setup_->getPlannerData( *planner_data );

        // Optionally display the search tree/graph or the samples
        if (!just_path)
        {
            // Visualize the explored space ---------------------------------------
            viewer_->displayGraph(cost_map_->cost_, planner_data);
            ros::Duration(0.1).sleep();

            // Visualize the sample locations -----------------------------------
            viewer_->displaySamples(cost_map_->cost_, planner_data);
            ros::Duration(0.1).sleep();
        }

        // Show basic solution ----------------------------------------
        if( false )
        {
            // Visualize the chosen path
            viewer_->displayResult( lightning_setup_->getSolutionPath(), RED, cost_map_->cost_ );
            ros::Duration(0.25).sleep();
        }

        // Interpolate -------------------------------------------------------
        if( true )
        {
            lightning_setup_->getSolutionPath().interpolate();

            // Visualize the chosen path
            viewer_->displayResult( lightning_setup_->getSolutionPath(), GREEN, cost_map_->cost_ );
            //      ros::Duration(0.25).sleep();
        }

        // Simplify solution ------------------------------------------------------
        if( false )
        {
            lightning_setup_->simplifySolution();

            // Visualize the chosen path
            viewer_->displayResult( lightning_setup_->getSolutionPath(), GREEN, cost_map_->cost_ );
            ros::Duration(0.25).sleep();
        }
    }

    /**
     * \brief Dump the entire database contents to Rviz
     */
    void displayDatabase()
    {
        // Display all of the saved paths
        std::vector<ompl::geometric::PathGeometric> paths;
        lightning_setup_->getAllPaths(paths);

        ROS_INFO_STREAM_NAMED("experience_database_test","Number of paths: " << paths.size());

        // Show all paths
        for (std::size_t i = 0; i < paths.size(); ++i)
        {        
            viewer_->displayResult( paths[i], RAND, cost_map_->cost_ );
            //ROS_INFO_STREAM_NAMED("temp","Sleeping after path " << i);
            ros::Duration(0.1).sleep();
        }        
    }


}; // end of class

} // namespace


// *********************************************************************************************************
// Main
// *********************************************************************************************************
int main( int argc, char** argv )
{
    ros::init(argc, argv, "ompl_rviz_viewer");
    ROS_INFO( "OMPL RViz Viewer with Lightning Framework ----------------------------------------- " );

    // Allow the action server to recieve and send ros messages
    ros::AsyncSpinner spinner(1);
    spinner.start();

    // Default argument values
    bool verbose = false;
    bool display_database = false;
    bool use_recall = true;
    std::string image_path;
    int runs = 1;

    if (argc > 1)
    {
        for (std::size_t i = 0; i < argc; ++i)
        {
            // Help mode
            if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0)
            {
                ROS_INFO_STREAM_NAMED("main","Usage: " << argv[0] << " --verbose --noRecall --image [image_file] --runs [num plans] --displayDatabase");
                return 0;
            }

            // Show all available plans
            if (strcmp(argv[i], "--displayDatabase") == 0)
            {
                ROS_INFO_STREAM_NAMED("main","Visualizing entire database");
                display_database = true;
            }

            // Check for verbose flag
            if (strcmp(argv[i], "--verbose") == 0)
            {
                ROS_INFO_STREAM_NAMED("main","Running in VERBOSE mode (slower)");
                verbose = true;
            }

            // Check if we should ignore the recall mechanism
            if (strcmp(argv[i], "--noRecall") == 0)
            {                
                ROS_INFO_STREAM_NAMED("main","NOT using recall for planning");
                use_recall = false;
            }

            // Check if user has passed in an image to read
            if (strcmp(argv[i], "--image") == 0)
            {
                i++; // get next arg
                image_path = argv[i];
            }

            // Check if user has passed in the number of runs to perform
            if (strcmp(argv[i], "--runs") == 0)
            {
                i++; // get next arg
                runs = atoi( argv[i] );
            }
        }
    }

    // Provide image if necessary
    if (image_path.empty()) // use default image
    {
        // Get image path based on package name
        image_path = ros::package::getPath("ompl_rviz_viewer");
        if( image_path.empty() )
        {
            ROS_ERROR( "Unable to get OMPL RViz Viewer package path " );
            return false;
        }

        // Seed random
        srand ( time(NULL) );

        // Choose random image
        switch( 1 ) //rand() % 4 )
        {
            case 0:
                image_path.append( "/resources/grand_canyon.ppm" );
                break;
            case 1:
                image_path.append( "/resources/height_map0.ppm" );
                break;
            case 2:
                image_path.append( "/resources/height_map1.ppm" );
                break;
            case 3:
                image_path.append( "/resources/height_map2.ppm" );
                break;
        }
    }

    // Create the planner
    ompl_rviz_viewer::OmplRvizLightning planner(verbose);

    // Load an image 
    ROS_INFO_STREAM_NAMED("main","Loading image " << image_path);
    planner.loadCostMapImage( image_path );

    // Display Contents of database if desires
    if (display_database)
    {
        planner.displayDatabase();
        return 0;
    }

    // Run the planner
    planner.plan( runs, use_recall );

    // Wait to let anything still being published finish
    ros::Duration(0.1).sleep();

    ROS_INFO_STREAM("Shutting down.");

    return 0;
}



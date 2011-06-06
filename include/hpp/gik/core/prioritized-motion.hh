#ifndef HPP_GIK_PRIORITIZED_MOTION_H
#define HPP_GIK_PRIORITIZED_MOTION_H

#include "jrl/mal/matrixabstractlayer.hh"
#include "gikTask/jrlGikMotionConstraint.h"

/**
\brief This object keeps a pointer to a CjrlGikMotionConstraint, and asdsociates a CjrlHumanoidDynamicRobot, a priority and a mask vector indicating active degrees of freedom for the motion constraint.
\ingroup motionsplan
 */
class ChppGikPrioritizedMotion
{
public:

    /**
    \brief Constructor
    */
    ChppGikPrioritizedMotion(CjrlDynamicRobot* inRobot, unsigned int inPriority, CjrlGikMotionConstraint* inMotionConstraint)
    {
        attRobot = inRobot;
        attPriority = inPriority;
        attWorkingJoints.resize(inRobot->numberDof());
        attWorkingJoints.clear();
        attMotionConstraint = inMotionConstraint;
    }

    /**
    \brief Set the joint mask put to work
     */
    void workingJoints(const vectorN& inJointsMask)
    {
        attWorkingJoints = inJointsMask;
    }
    /**
    \brief Get the joint mask put to work
     */
    const vectorN& workingJoints() const
    {
        return attWorkingJoints;
    }

    /**
    \brief Get the priority
     */
    unsigned int priority() const
    {
        return attPriority;
    }

    /**
    \brief Get the robot for this motion
     */
    CjrlDynamicRobot* robot()
    {
        return attRobot;
    }

    /**
    \brief Get a pointer to the motion constraint
    */
    virtual CjrlGikMotionConstraint* motionConstraint()
    {
        return attMotionConstraint;
    }

    /**
    \brief Destructor
    */
    virtual ~ChppGikPrioritizedMotion()
    {}


protected:

    CjrlGikMotionConstraint* attMotionConstraint;
    vectorN attWorkingJoints;
    unsigned int attPriority;
    CjrlDynamicRobot* attRobot;


};


#endif


#ifndef HPP_GIK_PRIORITIZED_MOTION_H
#define HPP_GIK_PRIORITIZED_MOTION_H

#include "MatrixAbstractLayer/MatrixAbstractLayer.h"
#include "gikTask/jrlGikMotionConstraint.h"

/**
\brief A ChppGikGenericTaskElement that returns a single motion constraint
 */
class ChppGikPrioritizedMotion
{
public:

    /**
    \brief Constructor
    */
    ChppGikPrioritizedMotion(CjrlHumanoidDynamicRobot* inRobot, unsigned int inPriority, CjrlGikMotionConstraint* inMotionConstraint)
    {
        attRobot = inRobot;
        attPriority = inPriority;
        attWorkingJoints.resize(inRobot->numberDof()-6);
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
    CjrlHumanoidDynamicRobot* robot()
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
    CjrlHumanoidDynamicRobot* attRobot;


};


#endif

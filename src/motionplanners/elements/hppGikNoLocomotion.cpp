#include "boost/numeric/ublas/vector_proxy.hpp"
#include "boost/numeric/ublas/matrix_proxy.hpp"
#include "hppGikTools.h"
#include "motionplanners/elements/hppGikNoLocomotion.h"

ChppGikNoLocomotion::ChppGikNoLocomotion(CjrlHumanoidDynamicRobot* inRobot, CjrlJoint* inSupportFoot, double inStartTime, double inEndTime, const vectorN& inWorkingJoints, unsigned int inPriority):ChppGikPrioritizedMotion(inRobot, inPriority, this)
{
    attConstraint = new ChppGikMotionPlanElement(inRobot, 0);
    vector3d pcom = inRobot->positionCenterOfMass();
    attComConstraint = new ChppGikComConstraint(*inRobot, pcom[0], pcom[1]);

    vector3d zer;
    zer[0] = zer[1] = zer[2] = 0;

    if (( inSupportFoot != inRobot->rightFoot()) &&( inSupportFoot != inRobot->leftFoot()))
    {
        attFootConstraint = new ChppGikTransformationConstraint(*inRobot, *(inRobot->rightFoot()), zer, inRobot->rightFoot()->currentTransformation());
        attSupportFoot = inRobot->leftFoot();
    }
    else
    {
        if ( inSupportFoot == inRobot->rightFoot() )
        {
            attFootConstraint = new ChppGikTransformationConstraint(*inRobot, *(inRobot->leftFoot()), zer, inRobot->leftFoot()->currentTransformation());
            attSupportFoot = inRobot->rightFoot();
        }
        else
        {
            attFootConstraint = new ChppGikTransformationConstraint(*inRobot, *(inRobot->rightFoot()), zer, inRobot->rightFoot()->currentTransformation());
            attSupportFoot = inRobot->leftFoot();
        }
    }

    attConstraint->addConstraint( attComConstraint );
    attConstraint->addConstraint( attFootConstraint );

    attStartTime = (inStartTime>0.0)?inStartTime:0.0;
    attEndTime = (inEndTime>attStartTime)?inEndTime:attStartTime;

    attZMP.resize(3);
    attZMP(0) = pcom[0];
    attZMP(1) = pcom[1];
    attZMP(2) = 0.0;

    if (inWorkingJoints.size() == inRobot->numberDof())
        attWorkingJoints = inWorkingJoints;
}
ChppGikNoLocomotion::~ChppGikNoLocomotion()
{
    delete attComConstraint;
    delete attConstraint;
    delete attFootConstraint;
}

CjrlGikMotionConstraint* ChppGikNoLocomotion::motionConstraint()
{
    return this;
}

CjrlGikMotionConstraint* ChppGikNoLocomotion::clone() const
{
    return (CjrlGikMotionConstraint*) new ChppGikNoLocomotion(attRobot, attSupportFoot, attStartTime, attEndTime, attWorkingJoints, attPriority);
}

CjrlDynamicRobot* ChppGikNoLocomotion::robot()
{
    return attRobot;
}

CjrlGikStateConstraint* ChppGikNoLocomotion::stateConstraintAtTime(double inTime)
{
    return attConstraint;
}

void ChppGikNoLocomotion::startTime(double inStartTime)
{
    attStartTime = (inStartTime<attEndTime && inStartTime>0.0)?inStartTime:attStartTime;
}
void ChppGikNoLocomotion::endTime(double inEndTime)
{
    attEndTime = (inEndTime>attStartTime)?inEndTime:attStartTime;
}

double ChppGikNoLocomotion::startTime()
{
    return attStartTime;
}

double ChppGikNoLocomotion::endTime()
{
    return attEndTime;
}

CjrlJoint* ChppGikNoLocomotion::supportFoot()
{
    return attSupportFoot;
}

const vectorN& ChppGikNoLocomotion::ZMP()
{
    return attZMP;
}



/******************************************************************************
*                    OROCOS Youbot simulator component                        *
*                                                                             *
*                         (C) 2011 Steven Bellens                             *
*                     steven.bellens@mech.kuleuven.be                         *
*                    Department of Mechanical Engineering,                    *
*                   Katholieke Universiteit Leuven, Belgium.                  *
*                                                                             *
*       You may redistribute this software and/or modify it under either the  *
*       terms of the GNU Lesser General Public License version 2.1 (LGPLv2.1  *
*       <http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html>) or (at your *
*       discretion) of the Modified BSD License:                              *
*       Redistribution and use in source and binary forms, with or without    *
*       modification, are permitted provided that the following conditions    *
*       are met:                                                              *
*       1. Redistributions of source code must retain the above copyright     *
*       notice, this list of conditions and the following disclaimer.         *
*       2. Redistributions in binary form must reproduce the above copyright  *
*       notice, this list of conditions and the following disclaimer in the   *
*       documentation and/or other materials provided with the distribution.  *
*       3. The name of the author may not be used to endorse or promote       *
*       products derived from this software without specific prior written    *
*       permission.                                                           *
*       THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR  *
*       IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED        *
*       WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE    *
*       ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,*
*       INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES    *
*       (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS       *
*       OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) *
*       HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,   *
*       STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING *
*       IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE    *
*       POSSIBILITY OF SUCH DAMAGE.                                           *
*                                                                             *
*******************************************************************************/

#include "simulator.hpp"

ORO_CREATE_COMPONENT(youbot::Simulator)

namespace youbot{
  Simulator::Simulator(std::string name) : TaskContext(name,PreOperational)
    ,m_level(0)
    ,m_posStateDimension(0)
    ,m_measDimension(0)
  {
    this->addPort("ctrl",ctrl_port).doc("Youbot control input");
    this->addPort("measurement",measurement_port).doc("Laser measurement output");
    this->addProperty("Level", m_level).doc("The level of continuity of the system model: 0 = cte position, 1= cte velocity ,... ");
    this->addProperty("SysNoiseMean", m_sysNoiseMean).doc("The mean of the noise on the marker system model");
    this->addProperty("SysNoiseCovariance", m_sysNoiseCovariance).doc("The covariance of the noise on the marker system model");
    this->addProperty("MeasModelMatrix", m_measModelMatrix).doc("Matrix for linear measurement model");
    this->addProperty("MeasModelCovariance", m_measModelCovariance).doc("Covariance matrix of additive Gaussian noise on measurement model");
    this->addProperty("MeasNoiseMean", m_measNoiseMean).doc("The mean of the noise on the marker measurement model");
    this->addProperty("MeasNoiseCovariance", m_measNoiseCovariance).doc("The covariance of the noise on the marker measurement model");
    this->addProperty("PosStateDimension", m_posStateDimension).doc("The dimension of the state space, only at position level");
    this->addProperty("MeasDimension", m_measDimension).doc("The dimension of the measurement space");
    this->addProperty("Period", m_period).doc("Period at which the system model gets updated");
    this->addProperty("State", m_state).doc("The system state: (x,y,theta) for level = 0, ...");
  }

  Simulator::~Simulator(){}

  bool Simulator::configureHook(){
#ifndef NDEBUG
    log(Debug) << "(Simulator) ConfigureHook entered" << endlog();
#endif
    // dimension of the state
    m_dimension = m_posStateDimension * (m_level+1);

#ifndef NDEBUG
    log(Debug) << "(Simulator) resizing initial mean and covariances" << endlog();
#endif
    m_measNoiseMean.resize(m_measDimension);
    m_measNoiseCovariance.resize(m_measDimension);

    /// resize class variables
    m_poseCovariance.resize(m_dimension);
    m_measurement.resize(m_measDimension);
    m_inputs.resize(4);

     /// make system model: a constant level'th derivative model
     /// state [x, y, theta]
     /// assumption: the 3 components of the youbot state [x,y,theta] are assumed to evolve independently
    ColumnVector sysNoiseVector = ColumnVector(m_level+1);
    sysNoiseVector = 0.0;
    SymmetricMatrix sysNoiseMatrixOne = SymmetricMatrix(m_level +1);
    sysNoiseMatrixOne = 0.0;
    Matrix sysNoiseMatrixNonSymOne = Matrix(m_level+1,m_level+1);
    sysNoiseMatrixNonSymOne = 0.0;
    for(int i =0 ; i<=m_level; i++)
    {
      sysNoiseVector(i+1) = pow(m_period,m_level-i+1)/double(factorial(m_level-i+1));
    }
    sysNoiseMatrixNonSymOne = (sysNoiseVector * sysNoiseVector.transpose()) * m_sysNoiseCovariance;
    sysNoiseMatrixNonSymOne.convertToSymmetricMatrix(sysNoiseMatrixOne);

    SymmetricMatrix sysNoiseMatrix = SymmetricMatrix(m_dimension);
    sysNoiseMatrix = 0.0;
    for(int i =0 ; i<=m_level; i++)
    {
      for(int j =0 ; j<=m_level; j++)
      {
        for (int k=1 ; k <=m_posStateDimension; k++)
        {
          sysNoiseMatrix(i*m_posStateDimension+k,j*m_posStateDimension+k)=sysNoiseMatrixOne(i+1,j+1);
        }
      }
    }

    Matrix sysModelMatrix = Matrix(m_dimension,m_dimension);
    sysModelMatrix = 0.0;
    ColumnVector sysNoiseMean = ColumnVector(m_dimension);
    sysNoiseMean = m_sysNoiseMean;

    Gaussian system_Uncertainty(sysNoiseMean, sysNoiseMatrix);
    m_sysPdf = new NonLinearAnalyticConditionalGaussianMobile(system_Uncertainty);
    m_sysModel = new AnalyticSystemModelGaussianUncertainty(m_sysPdf);

    /// make measurement model
    if(m_measDimension != m_measModelMatrix.rows())
    {
      log(Error) << "The size of the measurement does not fit the size of the measurement model matrix, measurement update not executed " << endlog();
      return false;
    }
    if(m_dimension != m_measModelMatrix.columns() )
    {
      log(Error) << "The size of the measurement model matrix does not fit the state dimension, measurement update not executed " << endlog();
      return false;
    }
    if(m_measDimension != m_measNoiseMean.rows() )
    {
      log(Error) << "The size of the measurement does not fit the size of the mean of te mesurement noise, measurement update not executed " << endlog();
      return false;
    }
    if(m_measDimension != m_measModelCovariance.rows() )
    {
      log(Error) << "The size of the measurement covariance matrix  does not fit the size of the mean of te mesurement noise, measurement update not executed " << endlog();
    }
    Gaussian measurement_Uncertainty(m_measNoiseMean, m_measModelCovariance);
    m_measPdf = new LinearAnalyticConditionalGaussian(m_measModelMatrix,measurement_Uncertainty);
    m_measModel = new LinearAnalyticMeasurementModelGaussianUncertainty(m_measPdf);
    return true;
  }

  bool Simulator::startHook(){
    //m_ctrl_input.linear.x = 0.0;
    //m_ctrl_input.linear.y = 0.5;
    //m_ctrl_input.angular.z = 0.0;
    return true;
  }

  void Simulator::updateHook(){
    if(ctrl_port.read(m_ctrl_input) != NoData){
      log(Debug) << "(Simulator - updateHook) Received control input - simulating system" << endlog();
      m_inputs[0] = m_ctrl_input.linear.x;
      m_inputs[1] = m_ctrl_input.linear.y;
      m_inputs[2] = m_ctrl_input.angular.z;
      m_inputs[3] = m_period;
      // simulate the system one time step
      m_state = m_sysModel->Simulate(m_state,m_inputs);
      // simulate a new measurement
      measurement_port.write(m_measModel->Simulate(m_state));
    }
  }

  int Simulator::factorial (int num)
  {
    if (num==1)
      return 1;
    return factorial(num-1)*num; // recursive call
  }

  void Simulator::stopHook(){
  }

  void Simulator::cleanupHook(){
  }
}
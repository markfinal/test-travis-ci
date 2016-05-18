/* +++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
   Copyright (c) 2013 The plumed team
   (see the PEOPLE file at the root of the distribution for a list of names)

   See http://www.plumed-code.org for more information.

   This file is part of plumed, version 2.

   plumed is free software: you can redistribute it and/or modify
   it under the terms of the GNU Lesser General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   plumed is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public License
   along with plumed.  If not, see <http://www.gnu.org/licenses/>.
+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++ */
#include "Bias.h"
#include "ActionRegister.h"
#include "core/PlumedMain.h"
#include "core/Atoms.h"
#include "core/Value.h"
#include <cmath>

using namespace std;

namespace PLMD{
namespace bias{

//+PLUMEDOC BIAS METAINFERENCE
/*
Calculate the Metainference Score for a set of experimental Data.

*/
//+ENDPLUMEDOC

class Metainference : public Bias
{
  const double sqrt2pi;
  const double sqrt2_div_pi;
  // experimental values
  vector<double> parameters;
  // noise type
  unsigned noise_type_;
  enum { GAUSS, MGAUSS, LTAIL};
  // scale is data scaling factor
  bool   doscale_;
  double scale_;
  double scale_min_;
  double scale_max_;
  double Dscale_;
  // sigma is data uncertainty
  vector<double> sigma_;
  double sigma_min_;
  double sigma_max_;
  double Dsigma_;
  // sigma_mean is uncertainty in the mean estimate
  double sigma_mean_;
  // temperature in kbt
  double   kbt_;
  // number of data points
  unsigned ndata_;
  // Monte Carlo stuff
  double   old_energy;
  unsigned MCsteps_;
  unsigned MCstride_;
  unsigned MCaccept_;
  long int MCfirst_;
  // output
  Value* valueBias;
  Value* valueScale;
  Value* valueAccept;
  vector<Value*> valueSigma;

  unsigned nrep_;
  unsigned replica_;

  double getEnergySPE(const double sigma, const double scale);
  double getEnergyGJE(const vector<double> &sigma, const double scale);
  void   doMonteCarlo();
  double getEnergyForceSPE();
  double getEnergyForceGJE();
  
public:
  explicit Metainference(const ActionOptions&);
  void calculate();
  static void registerKeywords(Keywords& keys);
};


PLUMED_REGISTER_ACTION(Metainference,"METAINFERENCE")

void Metainference::registerKeywords(Keywords& keys){
  Bias::registerKeywords(keys);
  keys.use("ARG");
  keys.add("optional","PARARG","the input for this action is the scalar output from other actions without derivatives."); 
  keys.add("optional","PARAMETERS","the parameters of the arguments in your function");
  keys.add("compulsory","NOISETYPE","functional form of the noise (GAUSS,MGAUSS,LTAIL)");
  keys.addFlag("SCALEDATA",false,"Set to TRUE if you want to sample a scaling factor common to all values and replicas.");  
  keys.addFlag("OPTSIGMAMEAN", false, "Set to minimize sigma_mean on the fly");
  keys.add("compulsory","SCALE0","initial value of the uncertainty parameter");
  keys.add("compulsory","SCALE_MIN","minimum value of the uncertainty parameter");
  keys.add("compulsory","SCALE_MAX","maximum value of the uncertainty parameter");
  keys.add("compulsory","DSCALE","maximum MC move of the uncertainty parameter");
  keys.add("compulsory","SIGMA0","initial value of the uncertainty parameter");
  keys.add("compulsory","SIGMA_MIN","minimum value of the uncertainty parameter");
  keys.add("compulsory","SIGMA_MAX","maximum value of the uncertainty parameter");
  keys.add("compulsory","DSIGMA","maximum MC move of the uncertainty parameter");
  keys.add("compulsory","SIGMA_MEAN","starting value for the uncertainty in the mean estimate");
  keys.add("optional","TEMP","the system temperature - this is only needed if code doesnt' pass the temperature to plumed");
  keys.add("optional","MC_STEPS","number of MC steps");
  keys.add("optional","MC_STRIDE","MC stride");
  componentsAreNotOptional(keys);
  useCustomisableComponents(keys);
  keys.addOutputComponent("bias","default","the instantaneous value of the bias potential");
  keys.addOutputComponent("sigma", "default","uncertainty parameter");
  keys.addOutputComponent("scale", "default","scale parameter");
  keys.addOutputComponent("accept","default","MC acceptance");
}

Metainference::Metainference(const ActionOptions&ao):
PLUMED_BIAS_INIT(ao), 
sqrt2pi(2.506628274631001),
sqrt2_div_pi(0.45015815807855),
doscale_(false),
ndata_(getNumberOfArguments()),
old_energy(0),
MCsteps_(1), 
MCstride_(1), 
MCaccept_(0), 
MCfirst_(-1)
{
  parseVector("PARAMETERS",parameters);
  if(parameters.size()!=static_cast<unsigned>(getNumberOfArguments())&&!parameters.empty())
    error("Size of PARAMETERS array should be either 0 or the same as of the number of arguments in ARG1");

  vector<Value*> arg2;
  parseArgumentList("PARARG",arg2);

  if(!arg2.empty()) {
    if(parameters.size()>0) error("It is not possible to use PARARG and PARAMETERS together");
    if(arg2.size()!=getNumberOfArguments()) error("Size of PARARG array should be the same as number for arguments in ARG");
    for(unsigned i=0;i<arg2.size();i++){
      parameters.push_back(arg2[i]->get()); 
      if(arg2[i]->hasDerivatives()==true) error("PARARG can only accept arguments without derivatives");
    }
  }

  if(parameters.size()!=getNumberOfArguments()) 
    error("PARARG or PARAMETERS arrays should include the same number of elements as the arguments in ARG");

  string stringa_noise;
  parse("NOISETYPE",stringa_noise);
  if(stringa_noise=="GAUSS")       noise_type_ = GAUSS; 
  else if(stringa_noise=="MGAUSS") noise_type_ = MGAUSS;
  else if(stringa_noise=="LTAIL")  noise_type_ = LTAIL;
  else error("Unkwnow noise type"); 

  parseFlag("SCALEDATA", doscale_);
  if(doscale_) {
    parse("SCALE0",scale_);
    parse("SCALE_MIN",scale_min_);
    parse("SCALE_MAX",scale_max_);
    parse("DSCALE",Dscale_);
  } else {
    scale_=1.0;
  }

  vector<double> readsigma;
  parseVector("SIGMA0",readsigma);
  if(noise_type_!=MGAUSS&&readsigma.size()>1) error("If you want to use more than one sigma you should add MULTISIGMA");

  parse("SIGMA_MIN",sigma_min_);
  parse("SIGMA_MAX",sigma_max_);
  parse("DSIGMA",Dsigma_);
  parse("MC_STEPS",MCsteps_);
  parse("MC_STRIDE",MCstride_);
  // get temperature
  double temp=0.0;
  parse("TEMP",temp);
  parse("SIGMA_MEAN",sigma_mean_);

  checkRead();

  if(temp>0.0) kbt_=plumed.getAtoms().getKBoltzmann()*temp;
  else kbt_=plumed.getAtoms().getKbT();

  if(readsigma.size()==getNumberOfArguments()) {
    sigma_.resize(getNumberOfArguments());
    sigma_=readsigma;
  } else if(readsigma.size()==1) {
    if(noise_type_==MGAUSS) sigma_.resize(getNumberOfArguments(),readsigma[0]);
    else sigma_.resize(1, readsigma[0]);
  } else {
    error("SIGMA0 can accept either one single value or as many values as the number of arguments (with MULTISIGMA)");
  } 

  // get number of replicas
  if(comm.Get_rank()==0) {
    nrep_ = multi_sim_comm.Get_size();
    replica_ = multi_sim_comm.Get_rank();
  } else {
    nrep_ = 0;
    replica_ = 0;
  }
  comm.Sum(&nrep_,1);
  comm.Sum(&replica_,1);

  // divide sigma_mean by the square root of the number of replicas
  sigma_mean_ /= sqrt(static_cast<double>(nrep_));

  // adjust for multiple-time steps
  MCstride_ *= getStride();

  switch(noise_type_) {
    case GAUSS:
      log.printf("  with gaussian noise and a single noise parameter for all the data\n");
      break;
    case MGAUSS:
      log.printf("  with gaussian noise and a noise parameter for each data point\n");
      break;
    case LTAIL:
      log.printf("  with long tailed gaussian noise and a single noise parameter for all the data\n");
      break;
  }

  if(doscale_) {
    log.printf("  sampling a common scaling factor with:\n");
    log.printf("    initial scale parameter %f\n",scale_);
    log.printf("    minimum scale parameter %f\n",scale_min_);
    log.printf("    maximum scale parameter %f\n",scale_max_);
    log.printf("    maximum MC move of scale parameter %f\n",Dscale_);
  }

  if(readsigma.size()==1) log.printf("  initial data uncertainty %f\n",sigma_[0]);
  else {
    log.printf("  initial data uncertainties");
    for(unsigned i=0;i<sigma_.size();++i) log.printf(" %f", sigma_[i]);
    log.printf("\n");
  }
  log.printf("  minimum data uncertainty %f\n",sigma_min_);
  log.printf("  maximum data uncertainty %f\n",sigma_max_);
  log.printf("  maximum MC move of data uncertainty %f\n",Dsigma_);
  log.printf("  uncertainty in the mean estimate %f\n",sigma_mean_);
  log.printf("  temperature of the system %f\n",kbt_);
  log.printf("  number of experimental data points %u\n",getNumberOfArguments());
  log.printf("  number of replicas %u\n",nrep_);
  log.printf("  MC steps %u\n",MCsteps_);
  log.printf("  MC stride %u\n",MCstride_);

  addComponent("bias");
  componentIsNotPeriodic("bias");
  valueBias=getPntrToComponent("bias");
  if(doscale_) { 
    addComponent("scale");  
    componentIsNotPeriodic("scale");
    valueScale=getPntrToComponent("scale");
  }
  addComponent("accept");
  componentIsNotPeriodic("accept");
  valueAccept=getPntrToComponent("accept");

  if(noise_type_==MGAUSS) {
    for(unsigned i=0;i<sigma_.size();++i){
      std::string num; Tools::convert(i,num);
      addComponent("sigma_"+num); componentIsNotPeriodic("sigma_"+num);
      valueSigma.push_back(getPntrToComponent("sigma_"+num));
    }
  } else {
    addComponent("sigma"); componentIsNotPeriodic("sigma");
    valueSigma.push_back(getPntrToComponent("sigma"));
  }
  // initialize random seed
  unsigned iseed;
  if(comm.Get_rank()==0) iseed = time(NULL)+replica_;
  else iseed = 0;     
  comm.Sum(&iseed, 1);
  srand(iseed);
}

double Metainference::getEnergySPE(const double sigma, const double scale){
  // calculate effective sigma
  const double smean2 = sigma_mean_*sigma_mean_;
  const double s = sqrt( sigma*sigma + smean2 );
  // cycle on arguments
  double ene = 0.0;
  for(unsigned i=0;i<getNumberOfArguments();++i){
    const double dev = scale*getArgument(i)-parameters[i]; 
    // argument
    const double a2 = 0.5*dev*dev + s*s;
    // increment energy
    ene += std::log( 2.0 * a2 / ( 1.0 - exp(- a2 / smean2) ) );
  }
  // add normalization and Jeffrey's prior
  ene += std::log(s) - static_cast<double>(ndata_)*std::log(sqrt2_div_pi*s);
  return kbt_ * ene;
}

double Metainference::getEnergyGJE(const vector<double> &sigma, const double scale){
  // cycle on arguments
  double ene = 0.0;
  const double smean2 = sigma_mean_*sigma_mean_;
  double ss = sigma[0]*sigma[0] + smean2; 
  for(unsigned i=0;i<getNumberOfArguments();++i){
    if(noise_type_==MGAUSS) ss = sigma[i]*sigma[i] + smean2; 
    const double dev = scale*getArgument(i)-parameters[i]; 
    ene += 0.5*dev*dev/ss + std::log(ss*sqrt2pi);
  }
  return kbt_ * ene;
}

void Metainference::doMonteCarlo(){
  // calculate old energy (first time only)
  if(MCfirst_==-1) {
    switch(noise_type_) {
      case GAUSS:
      case MGAUSS:
        old_energy = getEnergyGJE(sigma_,scale_);
        break;
      case LTAIL:
        old_energy = getEnergySPE(sigma_[0],scale_);
        break;
    }
  }
 
  // cycle on MC steps 
  for(unsigned i=0;i<MCsteps_;++i){
 
    // propose move for scale
    double new_scale = scale_;
    if(doscale_) {
      const double r1 = static_cast<double>(rand()) / RAND_MAX;
      const double ds1 = -Dscale_ + r1 * 2.0 * Dscale_;
      new_scale += ds1;
      // check boundaries
      if(new_scale > scale_max_){new_scale = 2.0 * scale_max_ - new_scale;}
      if(new_scale < scale_min_){new_scale = 2.0 * scale_min_ - new_scale;}
      // the scaling factor should be the same for all the replicas
      if(comm.Get_rank()==0) multi_sim_comm.Bcast(new_scale,0);
      comm.Bcast(new_scale,0);
    }
  
    // propose move for sigma
    vector<double> new_sigma(sigma_.size());
    for(unsigned j=0;j<sigma_.size();++j) {
      const double r2 = static_cast<double>(rand()) / RAND_MAX;
      const double ds2 = -Dsigma_ + r2 * 2.0 * Dsigma_;
      new_sigma[j] = sigma_[j] + ds2;
      // check boundaries
      if(new_sigma[j] > sigma_max_){new_sigma[j] = 2.0 * sigma_max_ - new_sigma[j];}
      if(new_sigma[j] < sigma_min_){new_sigma[j] = 2.0 * sigma_min_ - new_sigma[j];}
    }
 
    // calculate new energy
    double new_energy=0;
    switch(noise_type_) {
      case GAUSS:
      case MGAUSS:
        new_energy = getEnergyGJE(new_sigma,new_scale);
        break;
      case LTAIL:
        new_energy = getEnergySPE(new_sigma[0],new_scale);
        break;
    }
 
    // accept or reject
    const double delta = ( new_energy - old_energy ) / kbt_;
    // if delta is negative always accept move
    if( delta <= 0.0 ){
      old_energy = new_energy;
      scale_ = new_scale;
      sigma_ = new_sigma;
      MCaccept_++;
    // otherwise extract random number
    } else {
      const double s = static_cast<double>(rand()) / RAND_MAX;
      if( s < exp(-delta) ){
        old_energy = new_energy;
        scale_ = new_scale;
        sigma_ = new_sigma;
        MCaccept_++;
      }
    }
 
    if(doscale_) {
      // the scaling factor should be the same for all the replicas
      if(comm.Get_rank()==0) multi_sim_comm.Bcast(scale_,0);
      comm.Bcast(scale_,0);
    }
  }
  /* save the result of the sampling */
  if(doscale_) valueScale->set(scale_);
  for(unsigned i=0; i<sigma_.size(); i++) valueSigma[i]->set(sigma_[i]);
}

double Metainference::getEnergyForceSPE()
{
  double ene = 0.0;
  const unsigned narg=getNumberOfArguments();

  const double smean2 = sigma_mean_*sigma_mean_; 
  const double s = sqrt( sigma_[0]*sigma_[0] + smean2 );
  vector<double> f(narg,0);
  
  if(comm.Get_rank()==0){
   for(unsigned i=0;i<narg;++i){
     const double dev = scale_*getArgument(i)-parameters[i]; 
     const double a2 = 0.5*dev*dev + s*s;
     const double t = exp(-a2/smean2);
     const double dt = 1./t;
     const double it = 1./(1.-t);
     const double dit = 1./(1.-dt);
     ene += std::log(2.*a2*it);
     f[i] = -scale_*dev*(dit/smean2 + 1./a2);
   }
   // collect contribution to forces and energy from other replicas
   multi_sim_comm.Sum(&f[0],narg);
   multi_sim_comm.Sum(&ene,1);
   // add normalizations and priors of local replica
   ene += std::log(s) - static_cast<double>(ndata_)*std::log(sqrt2_div_pi*s);
  }
  // intra-replica summation
  comm.Sum(&f[0],narg);
  comm.Sum(&ene,1);

  for(unsigned i=0; i<narg; ++i) setOutputForce(i, kbt_ * f[i]);
  return ene;
}

double Metainference::getEnergyForceGJE()
{
  double ene = 0.0;

  const unsigned ssize = sigma_.size();
  vector<double> ss(ssize);
  vector<double> inv_s2(ssize, 0.);
  const double smean2 = sigma_mean_*sigma_mean_;

  for(unsigned i=0;i<ssize; ++i) {
    ss[i] = sigma_[i]*sigma_[i] + smean2;
    if(comm.Get_rank()==0) inv_s2[i] = 1.0/ss[i];
  }

  if(comm.Get_rank()==0) multi_sim_comm.Sum(&inv_s2[0],ssize); 
  comm.Sum(&inv_s2[0],ssize);  
  
  const unsigned narg=getNumberOfArguments();
  for(unsigned i=0;i<narg;++i){
    const double dev = scale_*getArgument(i)-parameters[i]; 
    unsigned sel_sigma=0;
    if(noise_type_==MGAUSS) sel_sigma=i;
    ene += 0.5*dev*dev*inv_s2[sel_sigma] + std::log(ss[sel_sigma]*sqrt2pi); 
    setOutputForce(i, -kbt_*dev*scale_*inv_s2[sel_sigma]);
  }
  return ene;
}

void Metainference::calculate(){
  /* MONTE CARLO */
  const long int step = getStep();
  if(step%MCstride_==0&&!getExchangeStep()) doMonteCarlo();
  // this is needed when restarting simulations
  if(MCfirst_==-1) MCfirst_=step;
  const double MCtrials = floor(static_cast<double>(step-MCfirst_) / static_cast<double>(MCstride_))+1.0;
  const double accept = static_cast<double>(MCaccept_) / static_cast<double>(MCsteps_) / MCtrials;
  valueAccept->set(accept);

  // calculate bias and forces
  double ene = 0; 
  switch(noise_type_) {
    case GAUSS:
    case MGAUSS:
      ene = getEnergyForceGJE();
      break;
    case LTAIL:
      ene = getEnergyForceSPE();
      break;
  }
  // set value of the bias
  valueBias->set(kbt_*ene);
}

}
}



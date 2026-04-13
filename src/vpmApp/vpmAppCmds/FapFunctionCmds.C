// SPDX-FileCopyrightText: 2023 SAP SE
//
// SPDX-License-Identifier: Apache-2.0
//
// This file is part of FEDEM - https://openfedem.org
////////////////////////////////////////////////////////////////////////////////

#include "vpmApp/vpmAppCmds/FapFunctionCmds.H"
#include "vpmApp/vpmAppCmds/FapDBCreateCmds.H"
#include "vpmApp/FapEventManager.H"
#include "vpmDB/FmEngine.H"
#include "vpmDB/FmfLinVar.H"
#include "vpmDB/FmSubAssembly.H"
#include "vpmDB/FmfDeviceFunction.H"
#include "vpmDB/FmfSinusoidal.H"
#include "vpmDB/FmSpringChar.H"
#include "vpmUI/Icons/FuiIconPixmaps.H"
#include "FFuLib/FFuAuxClasses/FFuaCmdItem.H"
#include <cmath>
#include <fstream>
#include <sstream>
#include <iomanip >
#include "vpmDB/FmDB.H"
#include "vpmDB/FmMechanism.H"
#include "FFuLib/FFuFileDialog.H"
#include "FFaLib/FFaOS/FFaFilePath.H"
#include "FFaLib/FFaDefinitions/FFaMsg.H"


namespace {
void createSpringChar(FmSpringChar::SpringCharUse useType);
void createFunc(FmMathFuncBase::FuncUse useType, bool defaultSine = false);
void convertFunc(FmMathFuncBase::FuncUse use);
void convertToEngine(FmMathFuncBase::FuncUse use);

// New: load sinusoidal definitions from file
void loadSinusoidsFromFile()
{
#ifdef FAP_DEBUG
  std::cout << "FapFunctionCmds::loadSinusoidsFromFile()" << std::endl;
#endif

  // Default directory: model path
  const std::string& absModelPath = FmDB::getMechanismObject()->getAbsModelFilePath();
  FFuFileDialog* dlg = FFuFileDialog::create(absModelPath, "Load Sinusoids", FFuFileDialog::FFU_OPEN_FILE, true);
  dlg->setTitle("Select sinusoidal definitions file");
  dlg->addFilter("Text file", Strings{ "txt", "asc", "csv" });
  dlg->addAllFilesFilter(true);

  //if (!dlg->runModal()) {
  //  delete dlg;
  //  return; // user cancelled
  //}

  std::vector<std::string> retFiles = dlg->execute();
  if (retFiles.empty()) return;
  std::string fileName = retFiles.front();
  delete dlg;
  if (fileName.empty()) return;

  // Make absolute if relative
  FFaFilePath::makeItAbsolute(fileName, absModelPath);

  std::ifstream ifs1("Sinusoidals.txt",std::ios::in);
  std::ifstream ifs(fileName.c_str(),std::ios::in);
  if (!ifs) {
    ListUI << "Could not open sinusoidal definitions file: " << fileName << "\n";
    return;
  }

  std::vector<FmfSinusoidal*> created;
  std::string line;
  int lineNo = 0;

	const double dPi = 4.0 * std::atan(1.0);

  while (std::getline(ifs, line)) {
    ++lineNo;
    // Trim leading spaces
    std::string tmp = line;
    size_t p = tmp.find_first_not_of(" \t\r\n");
    if (p == std::string::npos) continue; // empty
    if (tmp[p] == '#') continue; // comment

    std::istringstream iss(tmp);
    double omega = 0.0, periodDelay = 0.0, amplitude = 0.0, ampDisp = 0.0, maxTime = 0.0;
    std::string UITag("Un-initialized tag name");

    // Expect up to 6 values; require at least frequency and amplitude (or choose to require 3)
    if (!(iss >> std::quoted(UITag) >> omega >> periodDelay >> amplitude)) {
      ListUI << "Skipping line " << lineNo << ", insufficient values:" << tmp <<"\n";
      continue;
    }

    // optional values
    iss >> ampDisp >> maxTime;

    // Create function
    FmfSinusoidal* f = new FmfSinusoidal();
		f->setTag(UITag.c_str());
		f->setUserDescription(UITag.c_str());
		double freq = omega / (2.0 * dPi);
    f->setFrequency(freq);
    f->setPeriodDelay(periodDelay);
    f->setAmplitude(amplitude);
    f->setAmplitudeDisplacement(ampDisp);
    f->setMaxTime(maxTime);

    // Add to DB: keep as a plain function (unused) so user can assign later
    f->setFunctionUse(FmMathFuncBase::GENERAL);
    f->setParentAssembly(FapDBCreateCmds::getSelectedAssembly());
    f->connect();

    created.push_back(f);

    FmEngine* e = new FmEngine();
    e->setUserDescription(f->getUserDescription());
    e->setFunction(f);
    e->setParentAssembly(f->getParentAssembly());
    e->connect();
  }

  if (!created.empty()) {
    // Select the last created function (or choose another selection policy)
    FapEventManager::permTotalSelect(created.back());
    ListUI << "Imported " << created.size() << " sinusoidal definition(s) from " << fileName << "\n";
  } else {
    ListUI << "No sinusoidal definitions imported from " << fileName << "\n";
  }
}

} // anonymous namespace


#define LAMBDA_CREATE_SPRCHAR(USE) FFaDynCB0S([](){ createSpringChar(FmSpringChar::USE); })
#define LAMBDA_CREATE_FUNCTION(USE) FFaDynCB0S([](){ createFunc(FmMathFuncBase::USE); })
#define LAMBDA_CREATE_WAVE_FUNCTION FFaDynCB0S([](){ createFunc(FmMathFuncBase::WAVE_FUNCTION,true); })
#define LAMBDA_CONVERT_FUNCTION(USE) FFaDynCB0S([](){ convertFunc(FmMathFuncBase::USE); })
#define LAMBDA_CONVERT_ENGINE(USE) FFaDynCB0S([](){ convertToEngine(FmMathFuncBase::USE); })


void FapFunctionCmds::init()
{
  FFuaCmdItem* i;

#ifdef FAP_DEBUG
  std::cout <<"FapFunctionCmds::init()"<< std::endl;
#endif

  i = new FFuaCmdItem("cmdId_function_SpringCharTrans");
  i->setText("Adv. Spring Characteristics, Translation");
  i->setSmallIcon(spring_xpm);
  i->setActivatedCB(LAMBDA_CREATE_SPRCHAR(TRANSLATION));
  i->setGetSensitivityCB(FFaDynCB1S(FapCmdsBase::isModelEditable,bool&));

  i = new FFuaCmdItem("cmdId_function_SpringCharRot");
  i->setText("Adv. Spring Characteristics, Rotation");
  i->setSmallIcon(spring_xpm);
  i->setActivatedCB(LAMBDA_CREATE_SPRCHAR(ROTATION));
  i->setGetSensitivityCB(FFaDynCB1S(FapCmdsBase::isModelEditable,bool&));

  i = new FFuaCmdItem("cmdId_function_SprTransStiff");
  i->setText("Stiffness - Translation");
  i->setSmallIcon(K_Tspring_xpm);
  i->setActivatedCB(LAMBDA_CREATE_FUNCTION(SPR_TRA_STIFF));
  i->setGetSensitivityCB(FFaDynCB1S(FapCmdsBase::isModelEditable,bool&));

  i = new FFuaCmdItem("cmdId_function_SprTransForce");
  i->setText("Force - Translation");
  i->setSmallIcon(F_Tspring_xpm);
  i->setActivatedCB(LAMBDA_CREATE_FUNCTION(SPR_TRA_FORCE));
  i->setGetSensitivityCB(FFaDynCB1S(FapCmdsBase::isModelEditable,bool&));

  i = new FFuaCmdItem("cmdId_function_SprRotStiff");
  i->setText("Stiffness - Rotation");
  i->setSmallIcon(K_Rspring_xpm);
  i->setActivatedCB(LAMBDA_CREATE_FUNCTION(SPR_ROT_STIFF));
  i->setGetSensitivityCB(FFaDynCB1S(FapCmdsBase::isModelEditable,bool&));

  i = new FFuaCmdItem("cmdId_function_SprRotTorque");
  i->setText("Torque - Rotation");
  i->setSmallIcon(T_Rspring_xpm);
  i->setActivatedCB(LAMBDA_CREATE_FUNCTION(SPR_ROT_TORQUE));
  i->setGetSensitivityCB(FFaDynCB1S(FapCmdsBase::isModelEditable,bool&));

  i = new FFuaCmdItem("cmdId_function_DaTransCoeff");
  i->setText("Coefficient - Velocity");
  i->setSmallIcon(C_Tdamper_xpm);
  i->setActivatedCB(LAMBDA_CREATE_FUNCTION(DA_TRA_COEFF));
  i->setGetSensitivityCB(FFaDynCB1S(FapCmdsBase::isModelEditable,bool&));

  i = new FFuaCmdItem("cmdId_function_DaTransForce");
  i->setText("Force - Velocity");
  i->setSmallIcon(F_Tdamper_xpm);
  i->setActivatedCB(LAMBDA_CREATE_FUNCTION(DA_TRA_FORCE));
  i->setGetSensitivityCB(FFaDynCB1S(FapCmdsBase::isModelEditable,bool&));

  i = new FFuaCmdItem("cmdId_function_DaRotCoeff");
  i->setText("Coefficient - Rotational Vel.");
  i->setSmallIcon(C_Rdamper_xpm);
  i->setActivatedCB(LAMBDA_CREATE_FUNCTION(DA_ROT_COEFF));
  i->setGetSensitivityCB(FFaDynCB1S(FapCmdsBase::isModelEditable,bool&));

  i = new FFuaCmdItem("cmdId_function_DaRotTorque");
  i->setText("Torque - Rotational Velocity");
  i->setSmallIcon(T_Rdamper_xpm);
  i->setActivatedCB(LAMBDA_CREATE_FUNCTION(DA_ROT_TORQUE));
  i->setGetSensitivityCB(FFaDynCB1S(FapCmdsBase::isModelEditable,bool&));

  i = new FFuaCmdItem("cmdId_function_GeneralFunction");
  i->setText("Function");
  i->setSmallIcon(function_xpm);
  i->setActivatedCB(FFaDynCB0S(FapFunctionCmds::createGeneralFunction));
  i->setGetSensitivityCB(FFaDynCB1S(FapCmdsBase::isModelEditable,bool&));

  // New menu entry: "Sinusoidal definitions from File"
  i = new FFuaCmdItem("cmdId_function_ImportSinusoidsFromFile");
  i->setText("Sinusoidal definitions from File");
  //bh i->setSmallIcon(sinus); // reuse sinus icon if visible in this translation unit
  i->setSmallIcon(function_xpm); // reuse sinus icon if visible in this translation unit
  i->setActivatedCB(FFaDynCB0S(loadSinusoidsFromFile));
  i->setGetSensitivityCB(FFaDynCB1S(FapCmdsBase::isModelEditable,bool&));

  i = new FFuaCmdItem("cmdId_function_DriveFile");
  i->setText("Time history input file");
  i->setSmallIcon(timeHistInputFile_xpm);
  i->setActivatedCB(FFaDynCB0S(FapFunctionCmds::createDriveFile));
  i->setGetSensitivityCB(FFaDynCB1S(FapCmdsBase::isModelEditable,bool&));

  i = new FFuaCmdItem("cmdId_function_RoadFunction");
  i->setText("Road elevation");
  i->setSmallIcon(createRoad_xpm);
  i->setActivatedCB(LAMBDA_CREATE_FUNCTION(ROAD_FUNCTION));
  i->setGetSensitivityCB(FFaDynCB1S(FapCmdsBase::isModelEditable,bool&));

  i = new FFuaCmdItem("cmdId_function_WaveFunction");
  i->setText("Sea wave function");
  i->setSmallIcon(f_of_xt_xpm);
  i->setActivatedCB(LAMBDA_CREATE_WAVE_FUNCTION);
  i->setGetSensitivityCB(FFaDynCB1S(FapCmdsBase::isModelEditable,bool&));

  i = new FFuaCmdItem("cmdId_function_CurrFunction");
  i->setText("Sea current function");
  i->setSmallIcon(f_of_xt_xpm);
  i->setActivatedCB(LAMBDA_CREATE_FUNCTION(CURR_FUNCTION));
  i->setGetSensitivityCB(FFaDynCB1S(FapCmdsBase::isModelEditable,bool&));

  i = new FFuaCmdItem("cmdId_function_ConvertSprTransStiff");
  i->setText("Stiffness - Translation");
  i->setSmallIcon(K_Tspring_xpm);
  i->setActivatedCB(LAMBDA_CONVERT_FUNCTION(SPR_TRA_STIFF));
  i->setGetSensitivityCB(FFaDynCB1S(FapCmdsBase::isModelEditable,bool&));

  i = new FFuaCmdItem("cmdId_function_ConvertSprTransForce");
  i->setText("Force - Translation");
  i->setSmallIcon(F_Tspring_xpm);
  i->setActivatedCB(LAMBDA_CONVERT_FUNCTION(SPR_TRA_FORCE));
  i->setGetSensitivityCB(FFaDynCB1S(FapCmdsBase::isModelEditable,bool&));

  i = new FFuaCmdItem("cmdId_function_ConvertSprRotStiff");
  i->setText("Stiffness - Rotation");
  i->setSmallIcon(K_Rspring_xpm);
  i->setActivatedCB(LAMBDA_CONVERT_FUNCTION(SPR_ROT_STIFF));
  i->setGetSensitivityCB(FFaDynCB1S(FapCmdsBase::isModelEditable,bool&));

  i = new FFuaCmdItem("cmdId_function_ConvertSprRotTorque");
  i->setText("Torque - Rotation");
  i->setSmallIcon(T_Rspring_xpm);
  i->setActivatedCB(LAMBDA_CONVERT_FUNCTION(SPR_ROT_TORQUE));
  i->setGetSensitivityCB(FFaDynCB1S(FapCmdsBase::isModelEditable,bool&));

  i = new FFuaCmdItem("cmdId_function_ConvertDaTransCoeff");
  i->setText("Coefficient - Velocity");
  i->setSmallIcon(C_Tdamper_xpm);
  i->setActivatedCB(LAMBDA_CONVERT_FUNCTION(DA_TRA_COEFF));
  i->setGetSensitivityCB(FFaDynCB1S(FapCmdsBase::isModelEditable,bool&));

  i = new FFuaCmdItem("cmdId_function_ConvertDaTransForce");
  i->setText("Force - Velocity");
  i->setSmallIcon(F_Tdamper_xpm);
  i->setActivatedCB(LAMBDA_CONVERT_FUNCTION(DA_TRA_FORCE));
  i->setGetSensitivityCB(FFaDynCB1S(FapCmdsBase::isModelEditable,bool&));

  i = new FFuaCmdItem("cmdId_function_ConvertDaRotCoeff");
  i->setText("Coefficient - Rotational Velocity");
  i->setSmallIcon(C_Rdamper_xpm);
  i->setActivatedCB(LAMBDA_CONVERT_FUNCTION(DA_ROT_COEFF));
  i->setGetSensitivityCB(FFaDynCB1S(FapCmdsBase::isModelEditable,bool&));

  i = new FFuaCmdItem("cmdId_function_ConvertDaRotTorque");
  i->setText("Torque - Rotational Velocity");
  i->setSmallIcon(T_Rdamper_xpm);
  i->setActivatedCB(LAMBDA_CONVERT_FUNCTION(DA_ROT_TORQUE));
  i->setGetSensitivityCB(FFaDynCB1S(FapCmdsBase::isModelEditable,bool&));

  i = new FFuaCmdItem("cmdId_function_ConvertRoadFunction");
  i->setText("Road elevation");
  i->setSmallIcon(createRoad_xpm);
  i->setActivatedCB(LAMBDA_CONVERT_FUNCTION(ROAD_FUNCTION));
  i->setGetSensitivityCB(FFaDynCB1S(FapCmdsBase::isModelEditable,bool&));

  i = new FFuaCmdItem("cmdId_function_ConvertGeneralFunction");
  i->setText("Function");
  i->setSmallIcon(function_xpm);
  i->setActivatedCB(LAMBDA_CONVERT_ENGINE(GENERAL));
  i->setGetSensitivityCB(FFaDynCB1S(FapCmdsBase::isModelEditable,bool&));

  i = new FFuaCmdItem("cmdId_function_ConvertDriveFile");
  i->setText("Time history input file");
  i->setSmallIcon(timeHistInputFile_xpm);
  i->setActivatedCB(LAMBDA_CONVERT_ENGINE(DRIVE_FILE));
  i->setGetSensitivityCB(FFaDynCB1S(FapCmdsBase::isModelEditable,bool&));
}


namespace { // anonymous

void createSpringChar(FmSpringChar::SpringCharUse useType)
{
#ifdef FAP_DEBUG
  std::cout <<"FapFunctionCmds::createSpringChar() "<< useType << std::endl;
#endif

  FmSpringChar* sc = new FmSpringChar();
  sc->setSpringCharUse(useType);
  sc->setParentAssembly(FapDBCreateCmds::getSelectedAssembly());
  sc->connect();

  FapEventManager::permTotalSelect(sc);
}


void createFunc(FmMathFuncBase::FuncUse useType, bool defaultSine)
{
#ifdef FAP_DEBUG
  std::cout <<"FapFunctionCmds::createFunc() "<< useType << std::endl;
#endif

  FmMathFuncBase* f;
  if (defaultSine)
    f = new FmfSinusoidal();
  else
    f = new FmfLinVar();

  f->setFunctionUse(useType,true);
  f->setParentAssembly(FapDBCreateCmds::getSelectedAssembly());
  f->connect();

  FapEventManager::permTotalSelect(f);
}

} // anonymous namespace


void FapFunctionCmds::createDriveFile()
{
#ifdef FAP_DEBUG
  std::cout <<"FapFunctionCmds::createDriveFile()"<< std::endl;
#endif

  FmBase* subass = FapDBCreateCmds::getSelectedAssembly();
  FmMathFuncBase* f = new FmfDeviceFunction();
  f->setFunctionUse(FmMathFuncBase::DRIVE_FILE);
  f->setParentAssembly(subass);
  f->connect();

  FmEngine* e = new FmEngine();
  e->setFunction(f);
  e->setParentAssembly(subass);
  e->connect();

  FapEventManager::permTotalSelect(e);
}


void FapFunctionCmds::createGeneralFunction()
{
#ifdef FAP_DEBUG
  std::cout <<"FapFunctionCmds::createGeneralFunction()"<< std::endl;
#endif

  FmEngine* e = new FmEngine();
  e->setParentAssembly(FapDBCreateCmds::getSelectedAssembly());
  e->connect();

  FapEventManager::permTotalSelect(e);
}


namespace { // anonymous

void convertFunc(FmMathFuncBase::FuncUse use)
{
  std::vector<FmMathFuncBase*> selection;
  FapCmdsBase::getSelected(selection);

  for (FmMathFuncBase* f : selection)
    if (f->getFunctionUse() == FmMathFuncBase::NONE)
      if (f->setFunctionUse(use,true))
      {
        // The function was assigned a new userID due to conflict among
        // other functions with similar use. Must reconnect in order to
        // maintain the sorting of the linked list of functions.
        f->disconnect();
        f->connect();
      }
}


void convertToEngine(FmMathFuncBase::FuncUse use)
{
  std::vector<FmMathFuncBase*> selection;
  FapCmdsBase::getSelected(selection);

  for (FmMathFuncBase* f : selection)
    if (f->getFunctionUse() == FmMathFuncBase::NONE)
    {
      FmEngine* e = new FmEngine();
      f->setFunctionUse(use);
      e->setUserDescription(f->getUserDescription());
      e->setFunction(f);
      e->setParentAssembly(f->getParentAssembly());
      e->connect();
    }
}

} // anonymous namespace


void FapFunctionCmds::getConvertFuncSensitivity(bool& generalSensitivity,
						bool& convertSpringDamper,
						bool& convertDriveFile)
{
  FapCmdsBase::isModelEditable(generalSensitivity);
  if (!generalSensitivity) return;

  generalSensitivity = false;
  std::vector<FmMathFuncBase*> selection;
  if (!FapCmdsBase::getSelected(selection)) return;

  convertSpringDamper = convertDriveFile = true;

  for (FmMathFuncBase* func : selection)
    if (func->getFunctionUse() == FmMathFuncBase::NONE)
    {
      // Check if we can set to spring/damper function
      if (!func->isLegalSprDmpFunc())
	convertSpringDamper = false;

      // Check if we can set as time history input file
      if (!func->isOfType(FmfDeviceFunction::getClassTypeID()))
	convertDriveFile = false;
    }
    else // This is not an unused function
      return;

  // All selected functions are unused
  generalSensitivity = true;
}

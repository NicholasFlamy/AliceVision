
// Copyright (c) 2012, 2013, 2014 Pierre MOULON.

// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#include <cstdlib>

#include "openMVG/features/ImageDescriberCommon.hpp"
#include "openMVG/sfm/pipelines/global/sfm_global_engine_relative_motions.hpp"
#include "openMVG/system/timer.hpp"

using namespace openMVG;
using namespace openMVG::sfm;
using namespace openMVG::features;

#include "third_party/cmdLine/cmdLine.h"
#include "third_party/stlplus3/filesystemSimplified/file_system.hpp"


int main(int argc, char **argv)
{
  using namespace std;
  std::cout << std::endl
    << "-----------------------------------------------------------\n"
    << "Global Structure from Motion:\n"
    << "-----------------------------------------------------------\n"
    << "Open Source implementation of the paper:\n"
    << "\"Global Fusion of Relative Motions for "
    << "Robust, Accurate and Scalable Structure from Motion.\"\n"
    << "Pierre Moulon, Pascal Monasse and Renaud Marlet. "
    << " ICCV 2013." << std::endl
    << "------------------------------------------------------------"
    << std::endl;


  CmdLine cmd;

  std::string sSfM_Data_Filename;
  std::string describerMethod = "SIFT";
  std::string sMatchesDir;
  std::string sOutDir = "";
  std::string sOutSfMDataFilepath = "";
  int iRotationAveragingMethod = int (ROTATION_AVERAGING_L2);
  int iTranslationAveragingMethod = int (TRANSLATION_AVERAGING_SOFTL1);
  bool bRefineIntrinsics = true;

  cmd.add( make_option('i', sSfM_Data_Filename, "input_file") );
  cmd.add( make_option('d', describerMethod, "describerMethod") );
  cmd.add( make_option('m', sMatchesDir, "matchdir") );
  cmd.add( make_option('o', sOutDir, "outdir") );
  cmd.add( make_option('s', sOutSfMDataFilepath, "out_sfmdata_file") );
  cmd.add( make_option('r', iRotationAveragingMethod, "rotationAveraging") );
  cmd.add( make_option('t', iTranslationAveragingMethod, "translationAveraging") );
  cmd.add( make_option('f', bRefineIntrinsics, "refineIntrinsics") );

  try {
    if (argc == 1) throw std::string("Invalid parameter.");
    cmd.process(argc, argv);
  } catch(const std::string& s) {
    std::cerr << "Usage: " << argv[0] << '\n'
    << "[-i|--input_file] path to a SfM_Data scene\n"
    << "[-d|--describerMethod]\n"
    << "  (methods to use to describe an image):\n"
    << "   SIFT (default),\n"
    << "   SIFT_FLOAT to use SIFT stored as float,\n"
    << "   AKAZE_FLOAT: AKAZE with floating point descriptors,\n"
    << "   AKAZE_MLDB:  AKAZE with binary descriptors\n"
#ifdef HAVE_CCTAG
    << "   CCTAG3: CCTAG markers with 3 crowns\n"
    << "   CCTAG4: CCTAG markers with 4 crowns\n"
    << "   SIFT_CCTAG3: CCTAG markers with 3 crowns\n" 
    << "   SIFT_CCTAG4: CCTAG markers with 4 crowns\n" 
#endif
    << "[-m|--matchdir] path to the matches that corresponds to the provided SfM_Data scene\n"
    << "[-o|--outdir] path where the output data will be stored\n"
    << "\n[Optional]\n"
    << "[-s|--out_sfmdata_file] path of the output sfmdata file (default: $outdir/sfm_data.json)\n"
    << "[-r|--rotationAveraging]\n"
    << "\t 1 -> L1 minimization\n"
    << "\t 2 -> L2 minimization (default)\n"
    << "[-t|--translationAveraging]:\n"
    << "\t 1 -> L1 minimization\n"
    << "\t 2 -> L2 minimization of sum of squared Chordal distances\n"
    << "\t 3 -> SoftL1 minimization (default)\n"
    << "[-f|--refineIntrinsics]\n"
    << "\t 0-> intrinsic parameters are kept as constant\n"
    << "\t 1-> refine intrinsic parameters (default). \n"
    << std::endl;

    std::cerr << s << std::endl;
    return EXIT_FAILURE;
  }

  if(sOutSfMDataFilepath.empty())
    sOutSfMDataFilepath = stlplus::create_filespec(sOutDir, "sfm_data", "json");

  if (iRotationAveragingMethod < ROTATION_AVERAGING_L1 ||
      iRotationAveragingMethod > ROTATION_AVERAGING_L2 )  {
    std::cerr << "\n Rotation averaging method is invalid" << std::endl;
    return EXIT_FAILURE;
  }

  if (iTranslationAveragingMethod < TRANSLATION_AVERAGING_L1 ||
      iTranslationAveragingMethod > TRANSLATION_AVERAGING_SOFTL1 )  {
    std::cerr << "\n Translation averaging method is invalid" << std::endl;
    return EXIT_FAILURE;
  }

  // Load input SfM_Data scene
  SfM_Data sfm_data;
  if (!Load(sfm_data, sSfM_Data_Filename, ESfM_Data(VIEWS|INTRINSICS))) {
    std::cerr << std::endl
      << "The input SfM_Data file \""<< sSfM_Data_Filename << "\" cannot be read." << std::endl;
    return EXIT_FAILURE;
  }

  // Get imageDescriberMethodType
  EImageDescriberType describerMethodType = EImageDescriberType_stringToEnum(describerMethod);

  // Features reading
  FeaturesPerView featuresPerView;
  if (!loadFeaturesPerView(featuresPerView, sfm_data, sMatchesDir, describerMethodType)) {
    std::cerr << std::endl
      << "Invalid features." << std::endl;
    return EXIT_FAILURE;
  }
  // Matches reading
  std::shared_ptr<Matches_Provider> matches_provider = std::make_shared<Matches_Provider>();
  // Load the match file (try to read the two matches file formats)
  if(!(matches_provider->load(sfm_data, sMatchesDir, "e")))
  {
    std::cerr << std::endl << "Unable to load matches files from: " << sMatchesDir << std::endl;
    return EXIT_FAILURE;
  }

  if (sOutDir.empty())
  {
    std::cerr << "\nIt is an invalid output directory" << std::endl;
    return EXIT_FAILURE;
  }

  if (!stlplus::folder_exists(sOutDir))
    stlplus::folder_create(sOutDir);

  //---------------------------------------
  // Global SfM reconstruction process
  //---------------------------------------

  openMVG::system::Timer timer;
  GlobalSfMReconstructionEngine_RelativeMotions sfmEngine(
    sfm_data,
    sOutDir,
    stlplus::create_filespec(sOutDir, "Reconstruction_Report.html"));

  // Configure the featuresPerView & the matches_provider
  sfmEngine.SetFeaturesProvider(&featuresPerView);
  sfmEngine.SetMatchesProvider(matches_provider.get());

  // Configure reconstruction parameters
  sfmEngine.Set_bFixedIntrinsics(!bRefineIntrinsics);

  // Configure motion averaging method
  sfmEngine.SetRotationAveragingMethod(
    ERotationAveragingMethod(iRotationAveragingMethod));
  sfmEngine.SetTranslationAveragingMethod(
    ETranslationAveragingMethod(iTranslationAveragingMethod));

  if (sfmEngine.Process())
  {
    return EXIT_FAILURE;
  }

  // get the color for the 3D points
  if(!sfmEngine.Colorize())
  {
    std::cerr << "Colorize failed!" << std::endl;
  }

  std::cout << std::endl << " Total Ac-Global-Sfm took (s): " << timer.elapsed() << std::endl;

  std::cout << "...Generating SfM_Report.html" << std::endl;
  Generate_SfM_Report(sfmEngine.Get_SfM_Data(),
    stlplus::create_filespec(sOutDir, "SfMReconstruction_Report.html"));

  //-- Export to disk computed scene (data & visualizable results)
  std::cout << "...Export SfM_Data to disk." << std::endl;
  Save(sfmEngine.Get_SfM_Data(), sOutSfMDataFilepath, ESfM_Data(ALL));

  Save(sfmEngine.Get_SfM_Data(),
    stlplus::create_filespec(sOutDir, "cloud_and_poses", ".ply"),
    ESfM_Data(ALL));

  return EXIT_SUCCESS;
}

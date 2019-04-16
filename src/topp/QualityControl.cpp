// --------------------------------------------------------------------------
//                   OpenMS -- Open-Source Mass Spectrometry
// --------------------------------------------------------------------------
// Copyright The OpenMS Team -- Eberhard Karls University Tuebingen,
// ETH Zurich, and Freie Universitaet Berlin 2002-2018.
//
// This software is released under a three-clause BSD license:
//  * Redistributions of source code must retain the above copyright
//    notice, this list of conditions and the following disclaimer.
//  * Redistributions in binary form must reproduce the above copyright
//    notice, this list of conditions and the following disclaimer in the
//    documentation and/or other materials provided with the distribution.
//  * Neither the name of any author or any participating institution
//    may be used to endorse or promote products derived from this software
//    without specific prior written permission.
// For a full list of authors, refer to the file AUTHORS.
// --------------------------------------------------------------------------
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL ANY OF THE AUTHORS OR THE CONTRIBUTING
// INSTITUTIONS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
// EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
// PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
// OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
// WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
// OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
// ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
// --------------------------------------------------------------------------
// $Maintainer: Chris Bielow $
// $Authors: Tom Waschischeck $
// --------------------------------------------------------------------------

#include <OpenMS/APPLICATIONS/TOPPBase.h>
#include <OpenMS/CONCEPT/Exception.h>
#include <OpenMS/FORMAT/ConsensusXMLFile.h>
#include <OpenMS/FORMAT/FileHandler.h>
#include <OpenMS/FORMAT/IdXMLFile.h>
#include <OpenMS/FORMAT/FASTAFile.h>
#include <OpenMS/FORMAT/FeatureXMLFile.h>
#include <OpenMS/FORMAT/FileTypes.h>
#include <OpenMS/FORMAT/MzIdentMLFile.h>
#include <OpenMS/FORMAT/MzMLFile.h>
#include <OpenMS/FORMAT/MzTabFile.h>
#include <OpenMS/KERNEL/MSExperiment.h>
#include <OpenMS/METADATA/PeptideIdentification.h>
#include <OpenMS/QC/QCBase.h>
#include <OpenMS/QC/Contaminants.h>
#include <OpenMS/QC/Ms2IdentificationRate.h>
#include <OpenMS/QC/MissedCleavages.h>
#include <OpenMS/QC/TIC.h>
#include <cstdio>

using namespace OpenMS;
using namespace std;

//-------------------------------------------------------------
// Doxygen docu
//-------------------------------------------------------------
// We do not want this class to show up in the docu:


class TOPPQualityControl : public TOPPBase
{
public:
  TOPPQualityControl() : TOPPBase("QualityControl", "Does quality control for various input file types.", false)
  {
  }
protected:
  // this function will be used to register the tool parameters
  // it gets automatically called on tool execution
  void registerOptionsAndFlags_() override
  {
    registerInputFileList_("in_raw","<file>",{},"MzML input", false);
    setValidFormats_("in_raw", {"mzML"});
    registerInputFileList_("in_postFDR","<file>",{},"featureXML input", false);
    setValidFormats_("in_postFDR", {"featureXML"});
    registerInputFile_("in_contaminants","<file>","","Contaminant database input", false);
    setValidFormats_("in_contaminants", {"fasta"});
    registerInputFile_("in_consensus", "<file>","","ConsensusXML input, generated from given featureXMLs",false);
    setValidFormats_("in_consensus",{"ConsensusXML"});
    registerFlag_("MS2_id_rate:force_no_fdr", "forces the metric to run if fdr was not made, accept all pep_ids as target hits",false);
    //TODO get ProteinQuantifier output for PRT section
  }
  // the main_ function is called after all parameters are read
  ExitCodes main_(int, const char **) override
  {
    //-------------------------------------------------------------
    // parsing parameters
    //-------------------------------------------------------------
    //
    // Read input, check for same length and get that length
    QCBase::Status status;
    UInt64 number_exps(0);
    StringList in_raw = updateFileStatus_(status, number_exps, "in_raw", QCBase::Requires::RAWMZML);
    StringList in_postFDR = updateFileStatus_(status, number_exps, "in_postFDR", QCBase::Requires::POSTFDRFEAT);

    // load databases and other single file inputs
    String in_contaminants = getStringOption_("in_contaminants");
    FASTAFile fasta_file;
    vector<FASTAFile::FASTAEntry> contaminants;
    if (!in_contaminants.empty())
    {
      fasta_file.load(in_contaminants, contaminants);
      status |= QCBase::Requires::CONTAMINANTS;
    }
    String in_consensus = getStringOption_("in_consensus");
    ConsensusXMLFile consensus_file;
    ConsensusMap cmap;
    if (in_consensus.empty() &&
        (status.isSuperSetOf(QCBase::Requires::POSTFDRFEAT))) // FeatureXMLs need corresponding ConsensusXML
    {
      cerr
          << "FeatureXMLs given, but no ConsensusXML found. Please make sure to include this, if you want to do quality control for FeatureXMLs.\n";
      exit(MISSING_PARAMETERS);
    }
    if (!in_consensus.empty())
    {
      consensus_file.load(in_consensus, cmap);
    }

    // check flags
    bool fdr_flag = getFlag_("MS2_id_rate:force_no_fdr");

    // Instantiate the QC metrics
    Contaminants qc_contaminants;
    Ms2IdentificationRate qc_ms2ir;
    MissedCleavages qc_missed_cleavages;
    TIC qc_tic;


    map<Int64, PeptideIdentification *> map_to_id;
    
    // Loop through file lists
    for (Size i = 0; i < number_exps; ++i)
    {
      //-------------------------------------------------------------
      // reading input
      //-------------------------------------------------------------
      MzMLFile mzml_file;
      PeakMap exp;
      if (!in_raw.empty())
      {
        mzml_file.load(in_raw[i], exp);
      }

      FeatureXMLFile fxml_file;
      FeatureMap fmap;
      if (!in_postFDR.empty())
      {
        fxml_file.load(in_postFDR[i], fmap);
      }
      //-------------------------------------------------------------
      // calculations
      //-------------------------------------------------------------

      if (status.isSuperSetOf(qc_contaminants.requires()))
      {
        qc_contaminants.compute(fmap,contaminants);
      }

      if (status.isSuperSetOf(qc_ms2ir.requires()))
      {
        qc_ms2ir.compute(fmap, exp, fdr_flag);
      }

      if (status.isSuperSetOf(qc_missed_cleavages.requires()))
      {
        qc_missed_cleavages.compute(fmap);
      }

      if (status.isSuperSetOf(qc_tic.requires()))
      {
        qc_tic.compute(exp);
      }

      //-------------------------------------------------------------
      // Build the map to later find the original PepID in given ConsensusMap.
      //-------------------------------------------------------------
      for (Feature& feature : fmap)
      {
        fillPepIDMap_(map_to_id, feature.getPeptideIdentifications());
      }
      fillPepIDMap_(map_to_id, fmap.getUnassignedPeptideIdentifications());


      //-------------------------------------------------------------
      // Annotate calculated meta values from FeatureMap to given ConsensusMap
      //-------------------------------------------------------------

      // copy MetaValues of unassigned PepIDs
      copyPepIDMetaValues_(cmap.getUnassignedPeptideIdentifications(),map_to_id);

      // copy MetaValues of assigned PepIDs
      for (ConsensusFeature& cf : cmap)
      {
        copyPepIDMetaValues_(cf.getPeptideIdentifications(),map_to_id);
      }
    }

    //-------------------------------------------------------------
    // writing output
    //-------------------------------------------------------------
    MzTab mztab = MzTab::exportConsensusMapToMzTab(cmap,in_consensus,true,true);

    // Adding TIC information to meta data
    MzTabMetaData meta = mztab.getMetaData();
    map<Size, MzTabParameter> tics_as_meta_data;
    vector<MSChromatogram> tics = qc_tic.getResults();
    for (Size i = 0; i < tics.size(); ++i)
    {
      MzTabParameter tic;
      tic.setName("TIC_"+String(i+1));
      String value("[");
      value += String(tics[i][0].getRT()) + ", " + String(tics[i][0].getIntensity());
      for (Size j = 1; j < tics[i].size(); ++j)
      {
        value += ", " + String(tics[i][j].getRT()) + ", " + String(tics[i][j].getIntensity());
      }
      value += "]";
      tic.setValue(value);
      tics_as_meta_data[i+1] = tic;
    }
    meta.custom = tics_as_meta_data;
    mztab.setMetaData(meta);

    MzTabFile mztab_out;
    mztab_out.store("/home/togepitsch/Development/QC_test_outputs/consensus.mztab",mztab);
    return EXECUTION_OK;
  }

private:
  StringList updateFileStatus_(QCBase::Status& status, UInt64& number_exps, const String& port, const QCBase::Requires& req)
  {
    // since files are optional, leave function if non are provided by the user
    StringList files = getStringList_(port);
    if (!files.empty())
    {
      if (number_exps == 0) number_exps = files.size(); // Number of experiments is determined from first non empty file list.
      if (number_exps != files.size()) // exit if any file list has different length
      {
        cerr << port + ": invalid number of files. Expected were " << number_exps << ".\n";
        exit(ILLEGAL_PARAMETERS);
      }
      status |= req;
    }
    return files;
  }

  // templated function to copy all meta values from one object to another
  template <class FROM, class TO>
  //TODO get a MetaValue list to copy only those that have been set
  void copyMetaValues_(FROM& from, TO& to)
  {
    vector<String> keys;
    from.getKeys(keys);
    for(String& key : keys)
    {
      to.setMetaValue(key, from.getMetaValue(key));
    }
  }

  void copyPepIDMetaValues_(vector<PeptideIdentification>& pep_ids, const map<Int64,PeptideIdentification*>& map_to_id)
  {
    for (PeptideIdentification& pep_id : pep_ids)
    {
      if (!pep_id.metaValueExists("UID")) // PepID doesn't has ID, needs to have MetaValue
      {
        cerr << "No unique ID at unassigned peptideidentifications found. Please run PeptideIndexer with '-addUID'.\n";
        exit(ILLEGAL_PARAMETERS);
      }
      PeptideIdentification ref_pep_id = *(map_to_id.at(pep_id.getMetaValue("UID"))); // look up annotated PepID

      // copy all MetaValues that are at PepID level
      copyMetaValues_(ref_pep_id,pep_id);

      // copy all MetaValues that are at Hit level
      copyMetaValues_(ref_pep_id.getHits()[0],pep_id.getHits()[0]);
    }
  }

  void fillPepIDMap_(map<Int64,PeptideIdentification*>& map_to_id, vector<PeptideIdentification>& pep_ids)
  {
    for (PeptideIdentification& pep_id : pep_ids)
    {
      if (!pep_id.metaValueExists("UID")) // PepID doesn't has ID, needs to have MetaValue
      {
        cerr << "No unique ID at unassigned peptideidentifications found. Please run PeptideIndexer with '-addUID'.\n";
        exit(ILLEGAL_PARAMETERS);
      }
      map_to_id[pep_id.getMetaValue("UID")] = &pep_id;
    }
  }
};

// the actual main function needed to create an executable
int main(int argc, const char ** argv)
{
  TOPPQualityControl tool;
  return tool.main(argc, argv);
}
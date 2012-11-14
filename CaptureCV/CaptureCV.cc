#include "CaptureCV.h"



CaptureCV::CaptureCV(const std::string id, const BVS::Info& bvs)
	: BVS::Module(),
	id(id),
	logger(id),
	bvs(bvs),
	outputs(),
	inputs(),
	captures(),
	writers(),
	numNodes(bvs.config.getValue<int>(id+".numNodes", 1)),
	useVideo(bvs.config.getValue<bool>(id+".useVideo", false)),
	videoList(),
	useImages(bvs.config.getValue<bool>(id+".useImages", false)),
	imageNameScheme(bvs.config.getValue<std::string>(id+".imageNameScheme", "images/frame_{FRAME}_{NODE}.jpg")),
	nameParts(),
	imageCounter(1),
	captureMode(bvs.config.getValue<int>(id+".captureMode", -1)),
	captureFPS(bvs.config.getValue<double>(id+".captureFPS", -1.0)),
	recordVideo(bvs.config.getValue<bool>(id+".recordVideo", false)),
	recordFOURCC(bvs.config.getValue<std::string>(id+".recordFOURCC", "MJPG")),
	recordFPS(bvs.config.getValue<double>(id+".recordFPS", 0.0)),
	recordWidth(bvs.config.getValue<int>(id+".recordWidth", 0)),
	recordHeight(bvs.config.getValue<int>(id+".recordHeight", 0)),
	recordColor(bvs.config.getValue<int>(id+".recordColor", true)),
	fourcc()
{
	if (useVideo && useImages) LOG(0, "Cannot read from video and images, using video!");

	if (useVideo || recordVideo || useImages)
	{
		bvs.config.getValue<std::string>(id+".videoList", videoList);
		if ((int)videoList.size()<numNodes) LOG(0, "Not enough files given to read/write from/to!");
	}

	if (recordVideo)
	{
		if (recordFOURCC.length()!=4) LOG(0, "RecordFOURCC length must be 4!");
		fourcc = CV_FOURCC(recordFOURCC[0], recordFOURCC[1], recordFOURCC[2], recordFOURCC[3]);
		for (int i=0; i<numNodes; i++)
		{
			writers.emplace_back(cv::VideoWriter());
			inputs.emplace_back(new BVS::Connector<cv::Mat>("in"+std::to_string(i+1), BVS::ConnectorType::INPUT));
		}
	}
	else
	{
		for (int i=0; i<numNodes; i++)
		{
			captures.emplace_back(cv::VideoCapture(i));
			outputs.emplace_back(new BVS::Connector<cv::Mat>("out"+std::to_string(i+1), BVS::ConnectorType::OUTPUT));
			if (useVideo)
			{
				LOG(2, "Use file '" << videoList.at(i) << "' as source for node: " << i+1);
				captures.at(i).open(videoList.at(i));
			}
			else if (!useImages)
			{
				if (captureMode>=0) captures.at(i).set(CV_CAP_PROP_MODE, captureMode);
				if (captureFPS>=0) captures.at(i).set(CV_CAP_PROP_FPS, captureFPS);
				if (!captures.at(i).isOpened()) LOG(0, "Could not open camera: " << i << "!");
			}
		}
	}

	if (useImages)
	{
		std::string tmp = imageNameScheme;
		while (!tmp.empty())
		{
			LOG(0, tmp);
			size_t begin = tmp.find_first_of('{');
			size_t end = tmp.find_first_of('}');

			if (begin != 0 && begin != end) nameParts.push_back(tmp.substr(0, begin));
			if (begin<end) nameParts.push_back(tmp.substr(begin+1, end-begin-1));
			if (begin == std::string::npos || end == std::string::npos) nameParts.push_back(tmp);

			if (end == std::string::npos) tmp.clear();
			else tmp.erase(0, end+1);
		}
	}
}



CaptureCV::~CaptureCV()
{
	if (recordVideo)
	{
		for (auto wr: writers) wr.release();
		for (auto in: inputs) delete in;
	}
	else if (!useImages)
	{
		for (auto cap: captures) cap.release();
		for (auto out: outputs) delete out;
	}
}



BVS::Status CaptureCV::execute()
{
	if (recordVideo)
	{
		for (auto in: inputs) in->lockConnection();
		bool empty = false;
		for (auto in: inputs) if ((*in)->empty()) empty = true;
		if (!empty)
		{
			LOG(2, "Writing frame(s) to " << numNodes << " file(s)!");
			for (int i=0; i<numNodes; i++)
			{
				if (!writers[i].isOpened())
				{
					if (recordWidth==0) recordWidth = (**inputs[i]).cols;
					if (recordHeight==0) recordHeight = (**inputs[i]).rows;
					writers[i].open(videoList[i], fourcc, recordFPS, cv::Size(recordWidth, recordHeight), recordColor);
					if (!writers[i].isOpened()) LOG(0, "Could not open writer for '" << videoList[i]);
				}
				writers[i].write(**inputs[i]);
			}
		}
		for (auto in: inputs) in->unlockConnection();
	}
	else if (useVideo)
	{
		for (auto out: outputs) out->lockConnection();
		for (int i=0; i<numNodes; i++)
			if (!captures[i].read(**outputs[i])) exit(0); // SEE NOTE BELOW ABOUT EXIT...
		for (auto out: outputs) out->unlockConnection();
	}
	else if (useImages)
	{
		cv::Mat tmp;
		std::string filename;
		for (auto out: outputs) out->lockConnection();
		for (int i=0; i<numNodes; i++)
		{
			filename = getFileNameFromParts(imageCounter, i+1);
			LOG(3, "loading: " << filename);
			tmp = cv::imread(filename);
			if (tmp.empty())
			{
				LOG(0, "cannot open file: " << filename);
				// TODO what to do? signal shutdown or end of input? exit?
				exit(0); // THIS IS NOT THE CORRECT WAY, MODULES SHOULD NOT DO THIS
			}
			**outputs[i] = tmp;
		}
		for (auto out: outputs) out->unlockConnection();
		imageCounter++;
	}
	else
	{
		for (auto out: outputs) out->lockConnection();
		for (auto cap: captures) cap.grab();
		for (int i=0; i<numNodes; i++) captures[i].retrieve(**outputs[i]);
		for (auto out: outputs) out->unlockConnection();
	}

	return BVS::Status::OK;
}



BVS::Status CaptureCV::debugDisplay()
{
	return BVS::Status::OK;
}



std::string CaptureCV::getFileNameFromParts(int frame, int nodeID)
{
	std::string tmp;
	for (auto& part: nameParts)
	{
		if (part == "FRAME") tmp += std::to_string(frame);
		else if (part == "NODE") tmp += std::to_string(nodeID);
		else tmp += part;
	}

	return tmp;
}


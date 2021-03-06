#include "coco.h"
#include "mask.h"
#include <rapidjson/istreamwrapper.h>
#include <fstream>
#include <cassert>
#include <algorithm>
#include <cstring>
#include <iostream>
#include <ctime>


namespace coco{

Annotation::Annotation(const Value& value)
                      :id(value["id"].GetDouble()),
                       image_id(value["image_id"].GetInt()),
                       category_id(value["category_id"].GetInt()),
                       area(value["area"].GetDouble()),
                       iscrowd(value["iscrowd"].GetInt())
{
  if(value["segmentation"].IsObject()){
    if(value["segmentation"]["counts"].IsArray()){//uncompressed rle
      for(auto& coord : value["segmentation"]["counts"].GetArray())
        counts.push_back(coord.GetInt());
      size = std::make_pair(value["segmentation"]["size"][0].GetInt(), value["segmentation"]["size"][1].GetInt());
    }
    else if(value["segmentation"]["counts"].IsString()){//compressed rle
      compressed_rle = value["segmentation"]["counts"].GetString();
      size = std::make_pair(value["segmentation"]["size"][0].GetInt(), value["segmentation"]["size"][1].GetInt());
    }
    else{
      assert(false);
    }
  }
  else if(value["segmentation"].IsArray()){
    for(auto& polygon : value["segmentation"].GetArray()){
      std::vector<double> tmp;
      for(auto& coord : polygon.GetArray())
        tmp.push_back(coord.GetDouble());
      segmentation.push_back(tmp);
    }
  }
  else{
    assert(false);
  }
  
  for(auto& bbox_value : value["bbox"].GetArray())
    bbox.push_back(bbox_value.GetDouble());
}

Annotation::Annotation(): id(0), image_id(0), category_id(0), area(0), iscrowd(0){}

Image::Image(const Value& value)
            :id(value["id"].GetInt()),
             width(value["width"].GetInt()),
             height(value["height"].GetInt()),
             file_name(value["file_name"].GetString()){}

Image::Image(): id(0), width(0), height(0), file_name(""){}

Categories::Categories(const Value& value)
                      :id(value["id"].GetInt()),
                       name(value["name"].GetString()),
                       supercategory(value["supercategory"].GetString()){}

Categories::Categories(): id(0), name(""), supercategory(""){}

COCO::COCO(std::string annotation_file){
  std::cout << "loading annotations into memory...\n";
  time_t start = time(0);
  std::ifstream ifs(annotation_file);
  IStreamWrapper isw(ifs);
  std::cout << "Done : " << difftime(time(0), start) << "s\n";
  dataset.ParseStream(isw);
  assert(dataset.IsObject());
  CreateIndex();
}

COCO::COCO(){};

COCO::COCO(const COCO& other) :anns(other.anns), imgs(other.imgs), cats(other.cats), imgToAnns(other.imgToAnns), catToImgs(other.catToImgs){
  dataset.CopyFrom(other.dataset, dataset.GetAllocator());
}

COCO::COCO(COCO&& other) :anns(other.anns), imgs(other.imgs), cats(other.cats), imgToAnns(other.imgToAnns), catToImgs(other.catToImgs){
  dataset.CopyFrom(other.dataset.Move(), dataset.GetAllocator());
}

COCO COCO::operator=(const COCO& other){
  if(this != &other){
    anns = other.anns;
    imgs = other.imgs;
    imgToAnns = other.imgToAnns;
    catToImgs = other.catToImgs;
    dataset.CopyFrom(other.dataset, dataset.GetAllocator());
  }
  return *this;
}

COCO COCO::operator=(COCO&& other){
  if(this != &other){
    anns = other.anns;
    imgs = other.imgs;
    imgToAnns = other.imgToAnns;
    catToImgs = other.catToImgs;
    dataset.CopyFrom(other.dataset.Move(), dataset.GetAllocator());
  }
  return *this;
}

void COCO::CreateIndex(){
  std::cout << "creating index...\n";
  if(dataset.HasMember("annotations")){
    assert(dataset["annotations"].IsArray());
    for(auto& ann : dataset["annotations"].GetArray()){
      if(imgToAnns.count(ann["image_id"].GetInt())){ // if it exists
        imgToAnns[ann["image_id"].GetInt()].emplace_back(ann);
      }
      else{
        imgToAnns[ann["image_id"].GetInt()] = std::vector<Annotation> {Annotation(ann)};
      }
      anns[static_cast<int64_t>(ann["id"].GetDouble())] = Annotation(ann);
    }
  }

  if(dataset.HasMember("images")){
    for(auto& img : dataset["images"].GetArray()){
      imgs[img["id"].GetInt()] = Image(img);
    }
  }

  if(dataset.HasMember("categories")){
    assert(dataset["categories"].IsArray());
    for(auto& cat : dataset["categories"].GetArray()){
      cats[cat["id"].GetInt()] = Categories(cat);
    }
  }

  if(dataset.HasMember("categories") && dataset.HasMember("annotations")){
    for(auto& ann : dataset["annotations"].GetArray()){
      if(catToImgs.count(ann["category_id"].GetInt())){
        catToImgs[ann["category_id"].GetInt()].push_back(ann["image_id"].GetInt());
      }
      else{
        catToImgs.insert(
          {ann["category_id"].GetInt(), std::vector<int> {ann["image_id"].GetInt()}}
        );
      }
    }
  }//ann and cat
  std::cout << "index created!\n";
}

std::vector<int64_t> COCO::GetAnnIds(const std::vector<int> imgIds, 
                           const std::vector<int> catIds, 
                           const std::vector<float> areaRng, 
                           Crowd iscrowd)
{
  std::vector<int64_t> returnAnns;
  std::vector<Annotation> tmp_anns;
  if(imgIds.size() == 0 && catIds.size() == 0 && areaRng.size() == 0){
    for(auto& ann : dataset["annotations"].GetArray()){
      tmp_anns.emplace_back(ann);
    }
  }
  else{
    if(imgIds.size() != 0){
      for(auto& imgId : imgIds){
        if(imgToAnns.count(imgId)){//if it exists
          tmp_anns.insert(tmp_anns.end(), imgToAnns[imgId].begin(), imgToAnns[imgId].end());
        }
      }
    }
    else{
      for(auto& ann : dataset["annotations"].GetArray()){
        tmp_anns.emplace_back(ann);
      }
    }

    if(catIds.size() != 0){
      for(auto it = tmp_anns.begin(); it != tmp_anns.end(); ++it){
        if(std::find(catIds.begin(), catIds.end(), it->category_id) == catIds.end()){
          it = tmp_anns.erase(it);
        }
      }
    }

    if(areaRng.size() != 0){
      for(auto it = tmp_anns.begin(); it != tmp_anns.end(); ++it){
        if(it->area <= areaRng[0] || it->area >= areaRng[1]){
          it = tmp_anns.erase(it);
        }
      }
    }
  }

  if(iscrowd == none){
    for(auto& i : tmp_anns){
      returnAnns.push_back(i.id);
    }
  }
  else{
    bool check = (iscrowd == F ? false : true);
    for(auto& i : tmp_anns){
      if(i.iscrowd == check)
        returnAnns.push_back(i.id);
    }
  }
  return returnAnns;
}

std::vector<int> COCO::GetCatIds(const std::vector<std::string> catNms, 
                                 const std::vector<std::string> supNms, 
                                 const std::vector<int> catIds)
{
  std::vector<int> returnIds;
  std::vector<Categories> cats;
  for(auto& cat: dataset["categories"].GetArray()){
    cats.emplace_back(cat);
  }
  if(catNms.size() != 0){
    for(auto it = cats.begin(); it != cats.end();){
      if(std::find(catNms.begin(), catNms.end(), it->name) == catNms.end())
        it = cats.erase(it);
      else
        it++;
    }
  }

  if(supNms.size() != 0){
    for(auto it = cats.begin(); it != cats.end();){
      if(std::find(supNms.begin(), supNms.end(), it->supercategory) == supNms.end())
        it = cats.erase(it);
      else
        it++;
    }
  }

  if(catIds.size() != 0){
    for(auto it = cats.begin(); it != cats.end();){
      if(std::find(catIds.begin(), catIds.end(), it->id) == catIds.end())
        it = cats.erase(it);
      else
        it++;
    }
  }
  for(auto& cat: cats){
    returnIds.push_back(cat.id);
  }

  return returnIds;
  
}

std::vector<Annotation> COCO::LoadAnns(std::vector<int64_t> ids){
  std::vector<Annotation> returnAnns;
  for(auto& id : ids)
    returnAnns.push_back(anns[id]);

  return returnAnns;
}

std::vector<Image> COCO::LoadImgs(std::vector<int> ids){
  std::vector<Image> returnImgs;
  for(auto& id : ids)
    returnImgs.push_back(imgs[id]);

  return returnImgs;
}

COCO COCO::LoadRes(std::string res_file){
  COCO res = COCO();
  res.dataset.SetObject();
  //it only supports json file
  std::ifstream ifs(res_file);
  IStreamWrapper isw(ifs);
  Document anno;
  anno.ParseStream(isw);

  // std::vector<int> annsImgIds;
  // for(auto& ann : anno.GetArray())
  //   annsImgIds.push_back(ann["image_id"].GetInt());
  //no image id check
  //no caption implementation
  if(anno[0].HasMember("bbox") && !anno[0]["bbox"].Empty()){
    Document::AllocatorType& a = res.dataset.GetAllocator(); 
    Value copied_categories(dataset["categories"], a);
    res.dataset.AddMember("categories", copied_categories.Move(), a);
    for(int i = 0; i < anno.Size(); ++i){
      Value& bb = anno[i]["bbox"];
      int x1 = bb[0].GetDouble(), x2 = bb[0].GetDouble() + bb[1].GetDouble(), y1 = bb[1].GetDouble(), y2 = bb[1].GetDouble() + bb[3].GetDouble();
      if(!anno[i].HasMember("segmentation")){
        Value seg(kArrayType);
        Value coords(kArrayType);
        coords.PushBack(x1, a).PushBack(y1, a)
              .PushBack(x1, a).PushBack(y2, a)
              .PushBack(x2, a).PushBack(y1, a)
              .PushBack(x2, a).PushBack(y2, a);
        
        anno[i].AddMember("segmentation", seg.PushBack(coords, a).Move(), a);
      }
      anno[i].AddMember("area", bb[2].GetDouble() * bb[3].GetDouble(), a);
      anno[i].AddMember("id", i+1, a);
      anno[i].AddMember("iscrowd", 0, a);
    }
  }
  else if(anno[0].HasMember("segmentation")){
    Document::AllocatorType& a = res.dataset.GetAllocator(); 
    Value copied_categories(dataset["categories"], a);
    res.dataset.AddMember("categories", copied_categories.Move(), a);
    for(int i = 0; i < anno.Size(); ++i){
      std::vector<RLEstr> rlestr;
      rlestr.emplace_back(
        std::make_pair(anno[i]["segmentation"]["size"][0].GetInt(), anno[i]["segmentation"]["size"][1].GetInt()),
        anno[i]["segmentation"]["counts"].GetString()
      );
      
      std::vector<int64_t> seg = coco::area(rlestr);
      
      if(!anno[i].HasMember("bbox")){
        std::vector<double> bbox = coco::toBbox(rlestr);
        Value bb(kArrayType);
        for(auto& i : bbox)
          bb.PushBack(i, a);
        anno[i].AddMember("bbox", bb, a);
      }

      anno[i].AddMember("area", seg[0], a);
      anno[i].AddMember("id", i+1, a);
      anno[i].AddMember("iscrowd", 0, a);
    }
  }
  //no keypoints

  Document::AllocatorType& a = res.dataset.GetAllocator(); 
  Value copied_annotations(anno, a);
  res.dataset.AddMember("annotations", copied_annotations, a);
  
  res.CreateIndex();
  return res;
}

}//coco namespace

<?php

/**
 * @file
 * 
 * Encapsulates all CloudSearch communication relating to the administration of a Domain.
 * 
 * It's key responsiblity is to 
 * 		- Manage the creation, modification and removal of CloudSearch indexes
 * 		- Send documents (items) updates to CloudSearch
 * 		- Remove documents (items) from CloudSearch
 * 		- Provide status information about the Domain
 * 
 * Terminology
 * 
 * There are some important differences between the terminology of CloudSearch and Search API.
 * Below is a list of translations 
 * 
 *     ============================= 
 *     SEARCH API		CLOUDSEARCH
 *     =============================
 * 	   Index			Domain
 * 	   Field			Index
 *     Item				Document			
 * 
 */
   
class AWSCloudSearchDomain {
  private $_domain_name;
  private $_cloud_search_instance; 
  private $_domain_doc_endpoint;
  private $_document_batch_action = '/{API_VERSION}/documents/batch'; 
  private $_index_machine_name;
  
  public function __construct($options, $index_machine_name) {
    $this->_domain_name = $options['domain_name'];
    $this->_index_machine_name = $index_machine_name;
    $this->_document_batch_action = str_replace('{API_VERSION}', $options['cloudsearch_api_version'], $this->_document_batch_action); 
    $this->_cloud_search_instance = AWSCloudSearchHelper::getAWSCloudSearchInstance_Manual($options['aws_access_key'], $options['aws_secret_key'], $options['region']);
    if (!empty($options['document_endpoint'])) {
      $this->_domain_doc_endpoint = $options['document_endpoint'];  
    }
  }
  
  public function get_domain_details() {
    $details =  $this->_cloud_search_instance->describe_domains(array(
        'DomainNames' => $this->_domain_name
    ));
     
    return $details;
  }
  

  /**
   * Looks at all the various status fields for this Domain and returns if it's in an active state.
   * This is an important check before uploading documents to the domain as they won't get uploading
   * if it's not active.
   */
  public function is_active() {
    $details = $this->get_domain_details();
    if ($details->body->DescribeDomainsResult->DomainStatusList->member->Processing == 'true') {
      return FALSE;      
    }
    elseif ($details->body->DescribeDomainsResult->DomainStatusList->member->RequiresIndexDocuments == 'true') {
      return FALSE;  
    }
    else {
      return TRUE;
    }
  }
  
   /**
    * Gets the status of the Domain based on the various status fields for that Domain.
    *
    * @return string
    *   A string representation of the Domains status.  Will be
    *     - PROCESSING - Indexes have changed and are being processed
    *    - REQUIRES INDEXING - Index have change but have not yet bene processed
    *    - ACTIVE - Domain is fully available
    */
   public function get_status() {
    $details = $this->get_domain_details();

    if ($details->body->DescribeDomainsResult->DomainStatusList->member->Processing == 'true') {
      return 'PROCESSING';      
    }
    elseif ($details->body->DescribeDomainsResult->DomainStatusList->member->RequiresIndexDocuments == 'true') {
      return 'REQUIRES INDEXING';  
    }
    else {
      return 'ACTIVE';
    }    
  }
  
   /**
    * Get's the raw status object (DomainStatusList) from CloudSearch.
    *
    * @return DomainStatusList
    *	An object that holds the Domains status properties  
    *   
    */
  public function get_domain_status() {
    return  $this->get_domain_details()->body->DescribeDomainsResult->DomainStatusList->member;  
  }
  
  
  
   /**
    * Adds an Index into CloudSearch.
    * 
    * Note, there are terminology differences between SearchAPI and CloudSearch. A SearchAPI index is the entire
    * index, holding all the fields. Whereas CloudSearch index is a field and the domain is the equivalent of the
    * Search API index.  
    *
    * @param string $index_field
    *  	The name of the index
    *
    * @param string $index_type
    *    The Cloudsearch index type (literal, text, uint)
    *
    * @param string $facet
    *    Should this field be faceted
    *
    * @param string $result
    *    Is this field available to show in the results returned by CloudSearch 
    *
    * @param string $searchable
    *	Is this field searchable (text searchable)   
    *
    * @param string $source_field
    *   Used if this index should copy it's value from an existing index
    *  
    * @return boolean
    *	If the add index request worked.  
    *   
    */
  private function add_index($index_field, $index_type, $facet, $result, $searchable, $source_field = NULL) {
    $index_field = self::encode_index_name($this->_index_machine_name, $index_field);
    if ($source_field != NULL) {
      $source_field = self::encode_index_name($this->_index_machine_name, $source_field);
    }  
      
    $options = array();
    $options['IndexFieldName'] = $index_field;
    $options['IndexFieldType'] = $index_type;
    
    switch ($index_type) {
      case "text":
       $options['TextOptions'] = array('FacetEnabled' => $facet==TRUE ? "true" : "false", 
                                       'ResultEnabled' => $result==TRUE ? "true" : "false", );
        break;
      case "uint":
       break;
      case "literal":
       $options['LiteralOptions'] = array('FacetEnabled' => $facet==TRUE ? "true" : "false", 
                                       'ResultEnabled' => $result==TRUE ? "true" : "false", 
                                       'SearchEnabled' => $searchable==TRUE ? "true" : "false");      
       break;                 
      default:
        break;
    }
    
    if ($source_field!=NULL) {
      $options['SourceAttributes'] = array(array('SourceDataFunction' => 'Copy', 'SourceDataCopy' => array('SourceName' => $source_field)));
    }
    
    $response = $this->_cloud_search_instance->define_index_field($this->_domain_name, $options);
    
    return AWSCloudSearchHelper::checkAndReport(t('Add index'), $response, TRUE);
  }
  
  /**
   * Removes an Index into CloudSearch.
   * 
   * @param string $index_field
   *  	The name of the index to remove
   *  
   * @return boolean
   *	If the remove index request worked.  
   *   
   */
  private function remove_index($index_field) {
    $index_field = self::encode_index_name($this->_index_machine_name, $index_field);
    $response = $this->_cloud_search_instance->delete_index_field($this->_domain_name, $index_field);
    return AWSCloudSearchHelper::checkAndReport(t('Delete index'), $response, TRUE);
  }
  
  /**
   * Adds an Index into CloudSearch.
   * 
   * @return AWS Index details object
   *	All the index(field) information from the domain(index)  
   *   
   */  
  public function get_indexes() {
    return $this->_cloud_search_instance->describe_index_fields($this->_domain_name);
  }
  
  /**
   * Kicks off an Index Documents request within CloudSearch.  This will process any modification to
   * the index.
   * 
   * @return boolean
   *		returns true if the request was successful, false if it was not  
   *   
   */  
  private function index_documents() {
    $response = $this->_cloud_search_instance->index_documents($this->_domain_name);
    return AWSCloudSearchHelper::checkAndReport(t('Index documents'), $response, TRUE);  
  }
  

  /**
   *  Updates any modified CloudSearch indexes given the provided index  
   * 
   * @param SearchApiIndex $index 
   *  	The index to update
   * 
   * @return boolean
   *		returns true if the request was successful, false if it was not  
   *   
   */     
  public function update_indexes(SearchApiIndex $index) {
    $deleted_count = 0;
    $new_updated_count = 0;
    $ignored_count = 0;

    $success = TRUE;
    $errors = array();
    
    //populate an array of existing fields to remove any indexes
    $delete_fields = array();
    $standard_fields_found = FALSE;
    //get the indexes out of CloudSearch and check them against the modified ones in Drupal
    $new_fields = $index->getFields();
    $current_cs_indexes = $this->get_indexes()->body->DescribeIndexFieldsResult->IndexFields->member;
    foreach ($current_cs_indexes  as $index) {
      $field_name = self::decode_index_name($this->_index_machine_name, $index->Options->IndexFieldName);
      $found = FALSE;
      foreach ($new_fields as $field => $info) {
        if ($field == $field_name || 'sort_' . $field == $field_name) {
          $found = TRUE;
        }
      }
      if (!$found) {
        $delete_fields[] = $field_name;
      }     
    }
 
    foreach ($delete_fields as $field) {
      $response = $this->remove_index($field);
      $deleted_count++;
    }
    
    foreach ($new_fields as $name => $field_info) {
      //only do something with this field if it differs from the cloudsearch index in anyway
      $is_modified = $this->checkFieldChanged($name, $field_info, $current_cs_indexes);
      if ($is_modified == TRUE) {
          
        $cs_index_data = $this->convertSearchApiFieldTypeToAWSIndexType($field_info['type']);
    
        $index_type = $cs_index_data['index_type'];
        $facet = $cs_index_data['facet'];
        $searchable = $cs_index_data['searchable'];
        $sortable = $cs_index_data['sortable'];
      
        $this->add_index($name, $index_type, $facet, FALSE, $searchable, NULL);
        
        //have to created seperate fields for any index that needs to be sorted and faceted
        if ($sortable == TRUE && $facet == 1) {
          $sort_field_name = 'sort_' . $name;
          if ($field_info['type'] == 'decimal') {
            $index_type = 'uint'; //why? there is no decimal datatype in CloudSearch so change to uint, this isnt accurate but it's A solution, better way would be to encode the price as an alpha string so it can be sorted alphabetically 
          }
          $this->add_index($sort_field_name, $index_type, FALSE, TRUE, FALSE, $name);
        }
        $new_updated_count ++;
      }
      else {
        $ignored_count ++;
      }
    }
    
    $msg = t('CloudSearch Index update status: Deleted:') . $deleted_count . ' ' . t('Updated/New') . ': ' . $new_updated_count . ' ' . t('Not modified') . ':' . $ignored_count;
    drupal_set_message(check_plain($msg)); 
   
    //now tell CloudSearch to start indexing 
    return $this->index_documents();
  }

  /**
   * Helper function that determines if the field within Search API now differs from the CloudSearch Index
   */
  public function checkFieldChanged($name, $field, $cs_fields) {
    foreach ($cs_fields  as $index) {
      $field_name = self::decode_index_name($this->_index_machine_name, $index->Options->IndexFieldName);
      if ($name == $field_name) {
        $cs_index_data = $this->convertSearchApiFieldTypeToAWSIndexType($field['type']);
        $index_type = $cs_index_data['index_type'];
        $facet = $cs_index_data['facet'];
        $searchable = $cs_index_data['searchable'];
        $sortable = $cs_index_data['sortable'];
    
        if ((string)$index->Options->IndexFieldType != $index_type) {
          return TRUE;
        }
        if ($index_type == 'literal') {
          if ((string)$index->Options->LiteralOptions->FacetEnabled == "true" && $facet == FALSE) {
            return TRUE;
          }
          if ((string)$index->Options->LiteralOptions->SearchEnabled == "true" && $searchable == FALSE) {
            return TRUE;
          }            
        }
        if ($index_type == 'text') {
          if ((string)$index->Options->TextOptions->FacetEnabled == "true" && $facet == FALSE) {
            return TRUE;
          }
        }
        return FALSE;    
      }
    } 
    return TRUE; // if field not found then it's effectively modified
  } 
  
  /**
   * Helper - Takes the Search API field type and 
   * converts to AWS Index type including index options.
   * Dev Note: This may be a bit too primative, possibly look at alternative ways to identify 
   * 	       the index options.
   */
  public function convertSearchApiFieldTypeToAWSIndexType($type) {
   $return_data = array();
   switch ($type) {
      case "string":
        $return_data['index_type'] = 'literal';
        $return_data['facet'] = TRUE;
        $return_data['sortable'] = TRUE;
        $return_data['searchable'] = TRUE;
        break;
      case "date" :
        $return_data['index_type'] = 'uint';
        $return_data['facet'] = TRUE;
        $return_data['sortable'] = TRUE;
        $return_data['searchable'] = TRUE;      
        break;
      case "text" :
        $return_data['index_type'] = 'text';
        $return_data['facet'] = FALSE;
        $return_data['sortable'] = FALSE;
        $return_data['searchable'] = TRUE;        
        break;
      case "boolean":
        $return_data['index_type'] = 'literal';
        $return_data['facet'] = TRUE;
        $return_data['sortable'] = TRUE;
        $return_data['searchable'] = TRUE;      
        break;
      case "integer":
        $return_data['index_type'] = 'uint';
        $return_data['facet'] = TRUE;
        $return_data['sortable'] = TRUE;
        $return_data['searchable'] = TRUE;        
        break;
      case "decimal":
        $return_data['index_type'] = 'literal';
        $return_data['facet'] = TRUE;
        $return_data['sortable'] = TRUE;
        $return_data['searchable'] = TRUE;      
        break;
    default:
        $return_data['index_type'] = 'literal';
        $return_data['facet'] = TRUE;
        $return_data['sortable'] = TRUE;
        $return_data['searchable'] = TRUE;      
     }  
    return $return_data;  
  }
 
  /**
   *  Uploads documents into CloudSearch.  
   *   
   * Converts the items into Json SDF documents and submits to Document endpoint
   */  
  public function update_documents($items) {
    $json = array();
    $field_values = array();
    
    foreach ($items as $key => $item) {
      $values = array();
      foreach ($item as $name => $value) {
        $name = self::encode_index_name($this->_index_machine_name, $name);
        if ($value['value'] != NULL) {
          if ($value['type'] == 'tokens') { //CloudSearch doesn't support tokens like this so just combine it back into a string
             foreach ($value['value'] as $token) {
               $value .= $token['value'] . ' ';
             }
              $values[$name] = rtrim($value, ' ');            
          }
          elseif (is_array($value['value'])) {
            foreach ($value['value'] as $val)
              $values[$name][] = $val;
          }
          else {
             $values[$name] = $value['value'];
          }
        }
      } 
      $json[] = array(          
         'type' => 'add', 
         'id' => self::createId($this->_index_machine_name, $key), 
         'version' => time(), 
         'lang' => 'en', //do something better  
         'fields' => $values, 
      ); 
      
    }

    $status = $this->submit_json_request(json_encode($json));
    return TRUE;
    
    /* EXPERIMENTAL CODE FOR INFORMING THE CALLING THAT THE SEND FAILED
    if ($status != "200") {
      watchdog('aws_cloudsearch', 'Failed to call Document endpoint.  Got status code:' . $status, NULL, WATCHDOG_CRITICAL, $link = NULL);
      return FALSE;
    }
    else {
      return TRUE;
    }*/
    
  }


  /**
   *  Removes documents from CloudSearch  
   * 
   *  Converts ids into JSON SDF document and sends to Cloudsearch endpoint 
   * @param string<array> ids
   *  	An array of ids to remove 
   *   
   */ 
  public function remove_documents($ids) {
    $json = array();
    foreach ($ids as $id) {
      $json[] = array(
          'type' => 'delete', 
          'id' => self::encode_index_name($this->_index_machine_name, $id), 
          'version' => time(), 
       );      
    }
    
    $this->submit_json_request(json_encode($json));

  }
  
  /**
   * Add an ip address to the CloudSearch access policy 
   */
  public function add_ip_access_policy($ip_address) {
    $ip_address = $ip_address . "/32";
    $details = $this->get_domain_status();
    $search_arn = (string)$details->SearchService->Arn;
    $doc_arn = (string)$details->DocService->Arn;
   
    $response = $this->_cloud_search_instance->describe_service_access_policies($this->_domain_name);
    $options = json_decode($response->body->DescribeServiceAccessPoliciesResult->AccessPolicies->Options);


    //build up IAM Json document for doc and search endpoint
    $options->Statement[] = array('Effect' => 'Allow',
                                  'Action' => '*',
                                  'Resource' => $search_arn,
                                  'Condition' => array('IpAddress' => array('aws:SourceIp' => array($ip_address))));
    $options->Statement[] = array('Effect' => 'Allow',
                                  'Action' => '*',
                                  'Resource' => $doc_arn,
                                  'Condition' => array('IpAddress' => array('aws:SourceIp' => array($ip_address))));  
                                    
    $response = $this->_cloud_search_instance->update_service_access_policies($this->_domain_name, json_encode($options));
    
    return AWSCloudSearchHelper::checkAndReport(t('Add ip to access policy'), $response, FALSE);
  }
  
  /**
   *  Helper - takes the JSON SDF document and sends to the CloudSearch document endpoint  
   */  
  private function submit_json_request($json) {
    $ch = curl_init(); 
    curl_setopt($ch, CURLOPT_URL, 'http://' . $this->_domain_doc_endpoint . $this->_document_batch_action);//need endpoint action configured in settings
    curl_setopt($ch, CURLOPT_HEADER, 0); 
    curl_setopt($ch, CURLOPT_HTTPHEADER, array("Content-Type:application/json")); 
    curl_setopt($ch, CURLOPT_NOBODY, 1); 
    curl_setopt($ch, CURLOPT_RETURNTRANSFER, 1);
    curl_setopt($ch, CURLOPT_POST, 1); 
    curl_setopt($ch, CURLOPT_POSTFIELDS, $json); 
    $response = curl_exec($ch);
    $status = curl_getinfo($ch, CURLINFO_HTTP_CODE);
    curl_close($ch);
    return $status;
  }
   
  /**
   * Helper - Encodes a field name so that it's clearly identifiable in CloudSearch and seperated from other indexes 
   */
  public static function encode_index_name($index_name, $field_name) {
    return $index_name . '_' . str_replace(':', '____', $field_name);
  }
  /**
  * Helper - Decodes a CloudSearch index back into the Search API field name
  */
  public static function decode_index_name($index_name, $field_name) {
    return str_replace('____', ':', str_replace($index_name . '_', '', $field_name));
  }
  
  /**
   * Helper - Create the id for the document/item so that it's specific to an index 
   * Does this by simply combining the Index id and item id
   */  
  protected static function createId($index_id, $item_id) {
    return $index_id . '_' . $item_id;
  }
  
  
  
}
  
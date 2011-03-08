/*
 * Copyright (c) 2010 Vojtech Horky
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * - Redistributions of source code must retain the above copyright
 *   notice, this list of conditions and the following disclaimer.
 * - Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in the
 *   documentation and/or other materials provided with the distribution.
 * - The name of the author may not be used to endorse or promote products
 *   derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/** @addtogroup libusb
 * @{
 */
/** @file
 * @brief HID parser implementation.
 */
#include <usb/classes/hidparser.h>
#include <errno.h>
#include <stdio.h>
#include <malloc.h>
#include <mem.h>
#include <usb/debug.h>

#define USB_HID_NEW_REPORT_ITEM 1
#define USB_HID_NO_ACTION		2
#define USB_HID_UNKNOWN_TAG		-99

#define BAD_HACK_USAGE_PAGE		0x07

int usb_hid_report_parse_tag(uint8_t tag, uint8_t class, const uint8_t *data, size_t item_size,
                             usb_hid_report_item_t *report_item);
int usb_hid_report_parse_main_tag(uint8_t tag, const uint8_t *data, size_t item_size,
                             usb_hid_report_item_t *report_item);
int usb_hid_report_parse_global_tag(uint8_t tag, const uint8_t *data, size_t item_size,
                             usb_hid_report_item_t *report_item);
int usb_hid_report_parse_local_tag(uint8_t tag, const uint8_t *data, size_t item_size,
                             usb_hid_report_item_t *report_item);

void usb_hid_descriptor_print_list(link_t *head);
int usb_hid_report_reset_local_items();
void usb_hid_free_report_list(link_t *head);
int32_t usb_hid_report_tag_data_int32(const uint8_t *data, size_t size);
inline size_t usb_hid_count_item_offset(usb_hid_report_item_t * report_item, size_t offset);
int usb_hid_translate_data(usb_hid_report_item_t *item, const uint8_t *data, size_t j);
int usb_pow(int a, int b);

int usb_pow(int a, int b)
{
	switch(b) {
		case 0:
			return 1;
			break;
		case 1:
			return a;
			break;
		default:
			return a * usb_pow(a, b-1);
			break;
	}
}

/**
 *
 */
int usb_hid_parser_init(usb_hid_report_parser_t *parser)
{
   if(parser == NULL) {
	return -1;
   }

    list_initialize(&(parser->input));
    list_initialize(&(parser->output));
    list_initialize(&(parser->feature));

    return EOK;   
}


/** Parse HID report descriptor.
 *
 * @param parser Opaque HID report parser structure.
 * @param data Data describing the report.
 * @return Error code.
 */
int usb_hid_parse_report_descriptor(usb_hid_report_parser_t *parser, 
    const uint8_t *data, size_t size)
{
	size_t i=0;
	uint8_t tag=0;
	uint8_t item_size=0;
	int class=0;
	int ret;
	usb_hid_report_item_t *report_item=0;
	usb_hid_report_item_t *new_report_item;

	size_t offset=0;
	

	if(!(report_item=malloc(sizeof(usb_hid_report_item_t)))){
		return ENOMEM;
	}
	link_initialize(&(report_item->link));	

	while(i<size){	
		if(!USB_HID_ITEM_IS_LONG(data[i])){

			if((i+USB_HID_ITEM_SIZE(data[i]))>= size){
				return -1; // TODO ERROR CODE
			}
			
			tag = USB_HID_ITEM_TAG(data[i]);
			item_size = USB_HID_ITEM_SIZE(data[i]);
			class = USB_HID_ITEM_TAG_CLASS(data[i]);

			usb_log_debug2(
				"i(%u) data(%X) value(%X): TAG %u, class %u, size %u - ", i, 
			    data[i], usb_hid_report_tag_data_int32(data+i+1,item_size), 
			    tag, class, item_size);
			
			ret = usb_hid_report_parse_tag(tag,class,data+i+1,
			                         item_size,report_item);
			usb_log_debug2("ret: %u\n", ret);
			switch(ret){
				case USB_HID_NEW_REPORT_ITEM:
					// store report item to report and create the new one
					usb_log_debug("\nNEW REPORT ITEM: %X",tag);

					report_item->offset = offset;
					offset += report_item->count * report_item->size;
					
					switch(tag) {
						case USB_HID_REPORT_TAG_INPUT:
							usb_log_debug(" - INPUT\n");
							list_append(&(report_item->link), &(parser->input));
							break;
						case USB_HID_REPORT_TAG_OUTPUT:
							usb_log_debug(" - OUTPUT\n");
								list_append(&(report_item->link), &(parser->output));

							break;
						case USB_HID_REPORT_TAG_FEATURE:
							usb_log_debug(" - FEATURE\n");
								list_append(&(report_item->link), &(parser->feature));
							break;
						default:
						    usb_log_debug("\tjump over - tag %X\n", tag);
						    break;
					}

					/* clone current state table to the new item */
					if(!(new_report_item = malloc(sizeof(usb_hid_report_item_t)))) {
						return ENOMEM;
					}
					memcpy(new_report_item,report_item, sizeof(usb_hid_report_item_t));
					link_initialize(&(new_report_item->link));
					report_item = new_report_item;
					
					break;
				case USB_HID_REPORT_TAG_PUSH:
					// push current state to stack
					// not yet implemented
					break;
				case USB_HID_REPORT_TAG_POP:
					// restore current state from stack
					// not yet implemented						   
					break;
					
				default:
					// nothing special to do					
					break;
			}

			/* jump over the processed block */
			i += 1 + USB_HID_ITEM_SIZE(data[i]);
		}
		else{
			// TBD
			i += 3 + USB_HID_ITEM_SIZE(data[i+1]);
		}
		

	}
	
	return EOK;
}


/**
 * Parse input report.
 *
 * @param data Data for report
 * @param size Size of report
 * @param callbacks Callbacks for report actions
 * @param arg Custom arguments
 *
 * @return Error code
 */
int usb_hid_boot_keyboard_input_report(const uint8_t *data, size_t size,
	const usb_hid_report_in_callbacks_t *callbacks, void *arg)
{
	int i;
	usb_hid_report_item_t item;

	/* fill item due to the boot protocol report descriptor */
	// modifier keys are in the first byte
	uint8_t modifiers = data[0];

	item.offset = 2; /* second byte is reserved */
	item.size = 8;
	item.count = 6;
	item.usage_minimum = 0;
	item.usage_maximum = 255;
	item.logical_minimum = 0;
	item.logical_maximum = 255;

	if (size != 8) {
		return -1; //ERANGE;
	}

	uint8_t keys[6];
	for (i = 0; i < item.count; i++) {
		keys[i] = data[i + item.offset];
	}

	callbacks->keyboard(keys, 6, modifiers, arg);
	return EOK;
}

/**
 * Makes output report for keyboard boot protocol
 *
 * @param leds
 * @param output Output report data buffer
 * @param size Size of the output buffer
 * @return Error code
 */
int usb_hid_boot_keyboard_output_report(uint8_t leds, uint8_t *data, size_t size)
{
	if(size != 1){
		return -1;
	}

	/* used only first five bits, others are only padding*/
	*data = leds;
	return EOK;
}

/**
 *
 * @param Tag to parse
 * @param Report descriptor buffer
 * @param Size of data belongs to this tag
 * @param Current report item structe
 * @return Code of action to be done next
 */
int usb_hid_report_parse_tag(uint8_t tag, uint8_t class, const uint8_t *data, size_t item_size,
                             usb_hid_report_item_t *report_item)
{	
	int ret;
	
	switch(class){
		case USB_HID_TAG_CLASS_MAIN:

			if((ret=usb_hid_report_parse_main_tag(tag,data,item_size,report_item)) == EOK) {
				return USB_HID_NEW_REPORT_ITEM;
			}
			else {
				/*TODO process the error */
				return ret;
			   }
			break;

		case USB_HID_TAG_CLASS_GLOBAL:	
			return usb_hid_report_parse_global_tag(tag,data,item_size,report_item);
			break;

		case USB_HID_TAG_CLASS_LOCAL:			
			return usb_hid_report_parse_local_tag(tag,data,item_size,report_item);
			break;
		default:
			return USB_HID_NO_ACTION;
	}
}

/**
 * Parse main tags of report descriptor
 *
 * @param Tag identifier
 * @param Data buffer
 * @param Length of data buffer
 * @param Current state table
 * @return Error code
 */

int usb_hid_report_parse_main_tag(uint8_t tag, const uint8_t *data, size_t item_size,
                             usb_hid_report_item_t *report_item)
{
	switch(tag)
	{
		case USB_HID_REPORT_TAG_INPUT:
		case USB_HID_REPORT_TAG_OUTPUT:
		case USB_HID_REPORT_TAG_FEATURE:
			report_item->item_flags = *data;			
			return EOK;			
			break;
			
		case USB_HID_REPORT_TAG_COLLECTION:
			// TODO
			return USB_HID_NO_ACTION;
			break;
			
		case USB_HID_REPORT_TAG_END_COLLECTION:
			/* should be ignored */
			return USB_HID_NO_ACTION;
			break;
		default:
			return USB_HID_NO_ACTION;
	}

	return EOK;
}

/**
 * Parse global tags of report descriptor
 *
 * @param Tag identifier
 * @param Data buffer
 * @param Length of data buffer
 * @param Current state table
 * @return Error code
 */

int usb_hid_report_parse_global_tag(uint8_t tag, const uint8_t *data, size_t item_size,
                             usb_hid_report_item_t *report_item)
{
	// TODO take care about the bit length of data
	switch(tag)
	{
		case USB_HID_REPORT_TAG_USAGE_PAGE:
			report_item->usage_page = usb_hid_report_tag_data_int32(data,item_size);
			break;
		case USB_HID_REPORT_TAG_LOGICAL_MINIMUM:
			report_item->logical_minimum = usb_hid_report_tag_data_int32(data,item_size);
			break;
		case USB_HID_REPORT_TAG_LOGICAL_MAXIMUM:
			report_item->logical_maximum = usb_hid_report_tag_data_int32(data,item_size);
			break;
		case USB_HID_REPORT_TAG_PHYSICAL_MINIMUM:
			report_item->physical_minimum = usb_hid_report_tag_data_int32(data,item_size);
			break;			
		case USB_HID_REPORT_TAG_PHYSICAL_MAXIMUM:
			report_item->physical_maximum = usb_hid_report_tag_data_int32(data,item_size);
			break;
		case USB_HID_REPORT_TAG_UNIT_EXPONENT:
			report_item->unit_exponent = usb_hid_report_tag_data_int32(data,item_size);
			break;
		case USB_HID_REPORT_TAG_UNIT:
			report_item->unit = usb_hid_report_tag_data_int32(data,item_size);
			break;
		case USB_HID_REPORT_TAG_REPORT_SIZE:
			report_item->size = usb_hid_report_tag_data_int32(data,item_size);
			break;
		case USB_HID_REPORT_TAG_REPORT_COUNT:
			report_item->count = usb_hid_report_tag_data_int32(data,item_size);
			break;
		case USB_HID_REPORT_TAG_REPORT_ID:
			report_item->id = usb_hid_report_tag_data_int32(data,item_size);
			break;
		case USB_HID_REPORT_TAG_PUSH:
		case USB_HID_REPORT_TAG_POP:
			return tag;
			break;
			
		default:
			return USB_HID_NO_ACTION;
	}
	
	return EOK;
}

/**
 * Parse local tags of report descriptor
 *
 * @param Tag identifier
 * @param Data buffer
 * @param Length of data buffer
 * @param Current state table
 * @return Error code
 */
int usb_hid_report_parse_local_tag(uint8_t tag, const uint8_t *data, size_t item_size,
                             usb_hid_report_item_t *report_item)
{
	switch(tag)
	{
		case USB_HID_REPORT_TAG_USAGE:
			report_item->usage = usb_hid_report_tag_data_int32(data,item_size);
			break;
		case USB_HID_REPORT_TAG_USAGE_MINIMUM:
			report_item->usage_minimum = usb_hid_report_tag_data_int32(data,item_size);
			break;
		case USB_HID_REPORT_TAG_USAGE_MAXIMUM:
			report_item->usage_maximum = usb_hid_report_tag_data_int32(data,item_size);
			break;
		case USB_HID_REPORT_TAG_DESIGNATOR_INDEX:
			report_item->designator_index = usb_hid_report_tag_data_int32(data,item_size);
			break;
		case USB_HID_REPORT_TAG_DESIGNATOR_MINIMUM:
			report_item->designator_minimum = usb_hid_report_tag_data_int32(data,item_size);
			break;
		case USB_HID_REPORT_TAG_DESIGNATOR_MAXIMUM:
			report_item->designator_maximum = usb_hid_report_tag_data_int32(data,item_size);
			break;
		case USB_HID_REPORT_TAG_STRING_INDEX:
			report_item->string_index = usb_hid_report_tag_data_int32(data,item_size);
			break;
		case USB_HID_REPORT_TAG_STRING_MINIMUM:
			report_item->string_minimum = usb_hid_report_tag_data_int32(data,item_size);
			break;
		case USB_HID_REPORT_TAG_STRING_MAXIMUM:
			report_item->string_maximum = usb_hid_report_tag_data_int32(data,item_size);
			break;			
		case USB_HID_REPORT_TAG_DELIMITER:
			report_item->delimiter = usb_hid_report_tag_data_int32(data,item_size);
			break;
		
		default:
			return USB_HID_NO_ACTION;
	}
	
	return EOK;
}

/**
 * Converts raw data to int32 (thats the maximum length of short item data)
 *
 * @param Data buffer
 * @param Size of buffer
 * @return Converted int32 number
 */
int32_t usb_hid_report_tag_data_int32(const uint8_t *data, size_t size)
{
	unsigned int i;
	int32_t result;

	result = 0;
	for(i=0; i<size; i++) {
		result = (result | (data[i]) << (i*8));
	}

	return result;
}



/**
 * Prints content of given list of report items.
 *
 * @param List of report items
 * @return void
 */
void usb_hid_descriptor_print_list(link_t *head)
{
	usb_hid_report_item_t *report_item;
	link_t *item;
	
	if(head == NULL || list_empty(head)) {
	    printf("\tempty\n");
	    return;
	}
        
	for(item = head->next; item != head; item = item->next) {
                
		report_item = list_get_instance(item, usb_hid_report_item_t, link);

		printf("\tOFFSET: %X\n", report_item->offset);
		printf("\tCOUNT: %X\n", report_item->count);
		printf("\tSIZE: %X\n", report_item->size);
		printf("\tCONSTANT: %X\n", USB_HID_ITEM_FLAG_CONSTANT(report_item->item_flags));
		printf("\tUSAGE: %X\n", report_item->usage);
		printf("\tUSAGE PAGE: %X\n", report_item->usage_page);
		printf("\tLOGMIN: %X\n", report_item->logical_minimum);
		printf("\tLOGMAX: %X\n", report_item->logical_maximum);		
		printf("\tPHYMIN: %X\n", report_item->physical_minimum);		
		printf("\tPHYMAX: %X\n", report_item->physical_maximum);				
		printf("\n");		

	}


}
/**
 * Prints content of given descriptor in human readable format.
 *
 * @param Parsed descriptor to print
 * @return void
 */
void usb_hid_descriptor_print(usb_hid_report_parser_t *parser)
{
	printf("INPUT:\n");
	usb_hid_descriptor_print_list(&parser->input);
	
	printf("OUTPUT: \n");
	usb_hid_descriptor_print_list(&parser->output);
	
	printf("FEATURE:\n");	
	usb_hid_descriptor_print_list(&parser->feature);

}

/**
 * Releases whole linked list of report items
 *
 * 
 */
void usb_hid_free_report_list(link_t *head)
{
	return; 
	
	usb_hid_report_item_t *report_item;
	link_t *next;
	
	if(head == NULL || list_empty(head)) {		
	    return;
	}
	
	next = head->next;
	while(next != head) {
	
	    report_item = list_get_instance(next, usb_hid_report_item_t, link);
	    next = next->next;
	    
	    free(report_item);
	}
	
	return;
	
}

/** Free the HID report parser structure 
 *
 * @param parser Opaque HID report parser structure
 * @return Error code
 */
void usb_hid_free_report_parser(usb_hid_report_parser_t *parser)
{
	if(parser == NULL){
		return;
	}

	usb_hid_free_report_list(&parser->input);
	usb_hid_free_report_list(&parser->output);
	usb_hid_free_report_list(&parser->feature);

	return;
}

/** Parse and act upon a HID report.
 *
 * @see usb_hid_parse_report_descriptor
 *
 * @param parser Opaque HID report parser structure.
 * @param data Data for the report.
 * @param callbacks Callbacks for report actions.
 * @param arg Custom argument (passed through to the callbacks).
 * @return Error code.
 */ 
int usb_hid_parse_report(const usb_hid_report_parser_t *parser,  
    const uint8_t *data, size_t size,
    const usb_hid_report_in_callbacks_t *callbacks, void *arg)
{
	/*
	 *
	 * only key codes (usage page 0x07) will be processed
	 * other usages will be ignored 
	 */
	link_t *list_item;
	usb_hid_report_item_t *item;
	uint8_t *keys;
	size_t key_count=0;
	size_t i=0;
	size_t j=0;

	// get the size of result keycodes array
	list_item = parser->input.next;	   
	while(list_item != &(parser->input)) {

		item = list_get_instance(list_item, usb_hid_report_item_t, link);
		if(item->usage_page == BAD_HACK_USAGE_PAGE) {
			key_count += item->count;
		}

		list_item = list_item->next;
	}

	
	if(!(keys = malloc(sizeof(uint8_t) * key_count))){
		return ENOMEM;
	}

	// read data		
	list_item = parser->input.next;	   
	while(list_item != &(parser->input)) {

		item = list_get_instance(list_item, usb_hid_report_item_t, link);
		if(item->usage_page == BAD_HACK_USAGE_PAGE) {
			for(j=0; j<(size_t)(item->count); j++) {
				keys[i++] = usb_hid_translate_data(item, data,j);
			}
		}
		list_item = list_item->next;
	}

	callbacks->keyboard(keys, key_count, 0, arg);
	   
	free(keys);	
	return EOK;
	
}


int usb_hid_translate_data(usb_hid_report_item_t *item, const uint8_t *data, size_t j)
{
	int resolution;
	int offset;
	int part_size;
	
	int32_t value;
	int32_t mask;
	const uint8_t *foo;
	
	// now only common numbers llowed
	if(item->size > 32) {
		return 0;
	}

	if((item->physical_minimum == 0) && (item->physical_maximum ==0)) {
		item->physical_minimum = item->logical_minimum;
		item->physical_maximum = item->logical_maximum;		
	}

	resolution = (item->logical_maximum - item->logical_minimum) / ((item->physical_maximum - item->physical_minimum) * (usb_pow(10,(item->unit_exponent))));
	offset = item->offset + (j * item->size);
	
	// FIXME
	if((offset/8) != ((offset+item->size)/8)) {
		usb_log_debug2("offset %d\n", offset);
		
		part_size = ((offset+item->size)%8);
		usb_log_debug2("part size %d\n",part_size);

		// the higher one
		foo = data+(offset/8);
		mask =  ((1 << (item->size-part_size))-1);
		value = (*foo & mask) << part_size;

		usb_log_debug2("hfoo %x\n", *foo);
		usb_log_debug2("hmaska %x\n",  mask);
		usb_log_debug2("hval %d\n", value);		

		// the lower one
		foo = data+((offset+item->size)/8);
		mask =  ((1 << part_size)-1) << (8-part_size);
		value += ((*foo & mask) >> (8-part_size));

		usb_log_debug2("lfoo %x\n", *foo);
		usb_log_debug2("lmaska %x\n",  mask);
		usb_log_debug2("lval %d\n", ((*foo & mask) >> (8-(item->size-part_size))));		
		usb_log_debug2("val %d\n", value);
		
		
	}
	else {		
		foo = data+(offset/8);
		mask =  ((1 << item->size)-1) << (8-((offset%8)+item->size));
		value = (*foo & mask) >> (8-((offset%8)+item->size));

		usb_log_debug2("offset %d\n", offset);
		usb_log_debug2("foo %x\n", *foo);
		usb_log_debug2("maska %x\n",  mask);
		usb_log_debug2("val %d\n", value);				
	}

	usb_log_debug2("---\n\n");

	return (int)(((value - item->logical_minimum) / resolution) + item->physical_minimum);
	
}
/**
 * @}
 */

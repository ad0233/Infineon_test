use crc;

const FRAMEHEAD: u8 = 0xDA;
const FRAMECTRL: u8 = 0xFA;
const FRAMETAIL: u8 = 0x0A;

const X25: crc::Crc<u16> = crc::Crc::<u16>::new(&crc::CRC_16_IBM_SDLC);

pub struct SingleParse {
    recv_flag: bool,
    ctrl_flag: bool,
    rec_buffer: Vec<u8>,
    offset: usize,
    last_byte: u8,

    error_count: usize,
}

impl SingleParse {
    pub fn new(max_size: usize) -> Self {
        SingleParse {
            recv_flag: false,
            ctrl_flag: false,
            error_count: 0,
            offset: 0,
            last_byte: 0,
            rec_buffer: vec![0u8; max_size],
        }
    }

    #[allow(dead_code)]
    pub fn get_error_count(&self) -> usize {
        self.error_count
    }

    pub fn unpack(&mut self,mut data: u8) -> Option<Vec<u8>>{
        if data == FRAMEHEAD && self.last_byte == FRAMEHEAD {
            self.offset = 0;
            self.recv_flag = true;
            return None;
        }

        if self.recv_flag {
            if data == FRAMETAIL && self.last_byte == FRAMETAIL {
                self.recv_flag = false;

                if self.offset < 4 {
                    self.error_count += 1;
                    return None;
                }
                self.offset -= 3;

                let crc16 = X25.checksum(&self.rec_buffer[..self.offset]);
                let crc16_recv = (self.rec_buffer[self.offset] as u16) << 8 | self.rec_buffer[self.offset + 1] as u16;

                if crc16 == crc16_recv {
                    return Some(self.rec_buffer[..self.offset].to_vec());
                } else {
                    self.error_count += 1;
                    return None;
                }
            }
            
            if self.ctrl_flag {
                self.ctrl_flag = false;

                if data == FRAMEHEAD || data == FRAMETAIL || data == FRAMECTRL {
                    self.rec_buffer[self.offset] = data;
                    self.offset += 1;
                    data = FRAMECTRL;
                } else {
                    self.recv_flag = false;
                    self.error_count += 1;
                }
            } else {
                if data == FRAMECTRL {
                    self.ctrl_flag = true;
                } else {
                    self.rec_buffer[self.offset] = data;
                    self.offset += 1;
                }
            }

            if self.offset >= self.rec_buffer.len() {
                self.recv_flag = false;
                self.error_count += 1;
            }
        }
        self.last_byte = data;
        None
    }

    #[allow(dead_code)]
    pub fn pack(data: &[u8]) -> Vec<u8> {
        let crc16 = X25.checksum(data);
        let mut frame = Vec::with_capacity(data.len() + 128);
        frame.push(FRAMEHEAD);
        frame.push(FRAMEHEAD);
        for &byte in data {
            if byte == FRAMEHEAD || byte == FRAMETAIL || byte == FRAMECTRL {
                frame.push(FRAMECTRL);
            }
            frame.push(byte);
        }
        let crc16_h = (crc16 >> 8) as u8;
        if crc16_h == FRAMEHEAD || crc16_h == FRAMETAIL || crc16_h == FRAMECTRL {
            frame.push(FRAMECTRL);
        }
        frame.push(crc16_h);
        let crc16_l = (crc16 & 0xFF) as u8;
        if crc16_l == FRAMEHEAD || crc16_l == FRAMETAIL || crc16_l == FRAMECTRL {
            frame.push(FRAMECTRL);
        }
        frame.push(crc16_l);
        frame.push(FRAMETAIL);
        frame.push(FRAMETAIL);
        frame
    }
}

// test
#[cfg(test)]
mod test {
    use super::SingleParse;

    #[test]
    fn test_unpack() {
        let mut single_parse = SingleParse::new(1024);
        let data = vec![0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A];
        let frame = SingleParse::pack(&data);
        for &byte in &frame {
            let result = single_parse.unpack(byte);
            if let Some(result) = result {
                assert_eq!(result, data);
            }
        }
    }
}